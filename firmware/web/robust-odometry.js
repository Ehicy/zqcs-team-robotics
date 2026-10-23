/* Four rubber wheels, differential/skid steering. Wheel order is RF, LF, RR, LR.
 * Original implementation of standard differential-drive/SE(2) equations;
 * no Road Runner or ROS code is bundled. References and limits are below.
 *
 * https://rr.brott.dev/docs/v0-5/tour/kinematics/
 * https://docs.wpilib.org/en/stable/docs/software/kinematics-and-odometry/differential-drive-kinematics.html
 * https://github.com/cra-ros-pkg/robot_localization/blob/rolling-devel/src/ekf.cpp
 *
 * This is wheel odometry, NOT ground-truth localization or a proven fault
 * isolator. Two encoders on a side cannot vote out every plausible failure.
 * Slip, a stalled wheel and disconnected wiring can produce identical data.
 * In particular, zero counts at rest do not prove that a cable is connected.
 */

const SIDES = [[1, 3], [0, 2]]; // left, right
const validCount = v => Number.isSafeInteger(v);
const positive = (value, name) => {
  if (!Number.isFinite(value) || value <= 0) throw new RangeError(`${name} must be positive`);
  return value;
};
const countDelta = (a, b) => {
  // Encoder telemetry carries a wrapping 32-bit counter (signed or unsigned).
  let d = ((a % 4294967296) + 4294967296) % 4294967296 -
    ((b % 4294967296) + 4294967296) % 4294967296;
  if (d > 2147483647) d -= 4294967296;
  if (d < -2147483648) d += 4294967296;
  return d;
};

/**
 * All distances are mm; angles are unwrapped radians, positive left/CCW.
 * x points along the initial forward direction; y points left.
 *
 * new RobustOdometry({trackWidthMm: measuredEffectiveWidth,
 *   wheelCalibration: [{cpr,diameterMm,direction:1|-1}, ... four wheels]})
 *
 * update({counts:[RF,LF,RR,LR],timestampMs,validMask:15,
 *         commandedWheelMmS?:[RF,LF,RR,LR]})
 * `encoders` is an alias for `counts`. Commands are weak evidence of expected
 * movement, NEVER integrated as a replacement for measured distance.
 *
 * Invalid/recovered counts are rebased, so reconnecting a counter never
 * injects its missing accumulated distance into the recorded trajectory.
 * Only a missing *side* prevents that interval's pose integration. Healthy
 * wheels still update their own diagnostics, and automatically recover.
 * `pathComplete` stays false after a lost interval until reset(); later
 * `observable:true` does not retroactively reconstruct that lost movement.
 * A confidence is an engineering score, not a calibrated probability.
 * Call once per fresh encoder packet, not once per UI rendering of cached data.
 * Reset on board/session reset or mechanical calibration changes.
 */
export class RobustOdometry {
  constructor({
    wheelCalibration, trackWidthMm,
    maxWheelSpeedMmS = 2000, maxWheelAccelMmS2 = 10000,
    pairToleranceMmS = 80, pairRelativeTolerance = 0.3,
    stuckAfterMs = 250, minimumStuckTicks = 3,
    maxGapMs = 1000, recoverySamples = 5,
  } = {}) {
    if (!Array.isArray(wheelCalibration) || wheelCalibration.length !== 4)
      throw new TypeError('wheelCalibration must contain RF, LF, RR, LR');
    this.wheelCalibration = wheelCalibration.map((c, i) => {
      const cpr = positive(c?.cpr ?? c?.countsPerRev, `wheel ${i} cpr`);
      const diameterMm = positive(c?.diameterMm, `wheel ${i} diameterMm`);
      const direction = c.direction ?? c.encoderSign ?? 1;
      if (direction !== 1 && direction !== -1) throw new RangeError(`wheel ${i} direction must be +/-1`);
      return {cpr, diameterMm, direction, mmPerTick: Math.PI * diameterMm / cpr};
    });
    this.trackWidthMm = positive(trackWidthMm, 'trackWidthMm');
    this.maxWheelSpeedMmS = positive(maxWheelSpeedMmS, 'maxWheelSpeedMmS');
    this.maxWheelAccelMmS2 = positive(maxWheelAccelMmS2, 'maxWheelAccelMmS2');
    this.pairToleranceMmS = positive(pairToleranceMmS, 'pairToleranceMmS');
    this.pairRelativeTolerance = positive(pairRelativeTolerance, 'pairRelativeTolerance');
    this.stuckAfterMs = positive(stuckAfterMs, 'stuckAfterMs');
    this.minimumStuckTicks = positive(minimumStuckTicks, 'minimumStuckTicks');
    this.maxGapMs = positive(maxGapMs, 'maxGapMs');
    this.recoverySamples = positive(recoverySamples, 'recoverySamples');
    this.reset();
  }

  reset({xMm = 0, yMm = 0, headingRad = 0} = {}) {
    if (![xMm, yMm, headingRad].every(Number.isFinite)) throw new TypeError('pose must be finite');
    Object.assign(this, {xMm, yMm, headingRad, timestampMs:null,
      leftDistanceMm:0, rightDistanceMm:0, pathLengthMm:0, pathComplete:true});
    this.wheelTravelMm = [0,0,0,0];
    this.wheels = this.wheelCalibration.map(() => ({
      count:null, needsBaseline:true, lastMotionMs:null, previousVelocity:null,
      confidence:0, goodSamples:0, stuckEvidenceMm:0, reason:'baseline',
    }));
    this.last = this.snapshot({reason:'baseline', observable:false, integrated:false,
      validMask:0, usedMask:0, ambiguous:false, deltaMm:[0,0], wheelVelocityMmS:[null,null,null,null]});
    return this.last;
  }

  snapshot({reason, observable, integrated, validMask, usedMask, ambiguous,
    deltaMm, wheelVelocityMmS, dt = null, timestampMs = this.timestampMs}) {
    const degraded = validMask !== 15 || !observable || ambiguous || !this.pathComplete;
    const result = {
      timestampMs, xMm:this.xMm, yMm:this.yMm, headingRad:this.headingRad,
      pose:{xMm:this.xMm, yMm:this.yMm, headingRad:this.headingRad},
      leftDistanceMm:this.leftDistanceMm, rightDistanceMm:this.rightDistanceMm,
      sideTravelMm:[this.leftDistanceMm,this.rightDistanceMm],
      sideVelocityMmS:SIDES.map((side,index) => dt && side.some(i => usedMask & (1<<i)) ?
        deltaMm[index] / dt : null),
      wheelTravelMm:[...this.wheelTravelMm],
      pathLengthMm:this.pathLengthMm, pathComplete:this.pathComplete,
      confidence:this.wheels.map(w => w.confidence),
      wheelReasons:this.wheels.map(w => w.reason),
      validMask, usedMask, observable, integrated, degraded, ambiguous,
      missingSideMask:SIDES.reduce((mask,side,i) => side.some(w => usedMask & (1<<w)) ? mask : mask | (1<<i),0),
      estimated:degraded, reason, wheelVelocityMmS:[...wheelVelocityMmS],
      leftDeltaMm:deltaMm[0], rightDeltaMm:deltaMm[1],
    };
    this.last = result;
    return result;
  }

  invalidate(i, reason) {
    const w = this.wheels[i];
    w.confidence = 0; w.goodSamples = 0; w.previousVelocity = null;
    w.reason = reason; w.needsBaseline = true;
  }

  rebase(counts, timestampMs, validMask, reason) {
    for (let i = 0; i < 4; i++) {
      const w = this.wheels[i], available = !!(validMask & (1 << i)) && validCount(counts[i]);
      w.count = validCount(counts[i]) ? counts[i] : null;
      w.needsBaseline = !available; w.previousVelocity = null;
      w.lastMotionMs = timestampMs; w.stuckEvidenceMm = 0;
      w.goodSamples = 0; w.confidence = 0; w.reason = available ? reason : 'unavailable';
    }
    this.timestampMs = timestampMs;
    return this.snapshot({reason, observable:false, integrated:false,
      validMask:0, usedMask:0, ambiguous:false, deltaMm:[0,0], wheelVelocityMmS:[null,null,null,null]});
  }

  update({counts, encoders, timestampMs, validMask = 15, commandedWheelMmS} = {}) {
    counts ??= encoders;
    if (!Array.isArray(counts) && !ArrayBuffer.isView(counts)) throw new TypeError('counts must be a four element array');
    if (counts.length !== 4) throw new TypeError('counts must contain RF, LF, RR, LR');
    if (!Number.isFinite(timestampMs)) throw new TypeError('timestampMs must be finite');
    if (!Number.isInteger(validMask) || validMask < 0 || validMask > 15) throw new RangeError('validMask must be 0..15');
    if (commandedWheelMmS !== undefined &&
      (commandedWheelMmS.length !== 4 || !Array.from(commandedWheelMmS).every(Number.isFinite)))
      throw new TypeError('commandedWheelMmS must contain four finite speeds');
    if (this.timestampMs === null) return this.rebase(counts, timestampMs, validMask, 'baseline');
    const elapsedMs = timestampMs - this.timestampMs;
    // Ignore duplicate/out-of-order packets without changing any count baseline.
    if (elapsedMs <= 0) return {...this.last, integrated:false, reason:'stale-timestamp'};
    if (elapsedMs > this.maxGapMs) {
      this.pathComplete = false;
      return this.rebase(counts, timestampMs, validMask, 'sample-gap');
    }
    const dt = elapsedMs / 1000, velocities = [null,null,null,null], deltas = [null,null,null,null];
    const priorVelocity = this.wheels.map(w => w.previousVelocity);
    for (let i = 0; i < 4; i++) {
      const w = this.wheels[i], c = this.wheelCalibration[i], count = counts[i];
      if (!(validMask & (1 << i)) || !validCount(count)) {
        if (validCount(count)) w.count = count;
        this.invalidate(i, 'unavailable'); continue;
      }
      if (w.needsBaseline || w.count === null) {
        w.count = count; w.needsBaseline = false; w.lastMotionMs = timestampMs;
        w.stuckEvidenceMm = 0; w.reason = 'rebase'; continue;
      }
      const delta = countDelta(count, w.count) * c.mmPerTick * c.direction;
      w.count = count; // Always discard a rejected interval; never replay it later.
      const velocity = delta / dt, quantization = 2 * c.mmPerTick / dt;
      if (Math.abs(velocity) > this.maxWheelSpeedMmS + quantization) {
        this.invalidate(i, 'speed-outlier'); continue;
      }
      if (w.previousVelocity !== null &&
        Math.abs(velocity - w.previousVelocity) > this.maxWheelAccelMmS2 * dt + quantization) {
        // Reject one sample and reset the derivative history, but retain the
        // current counter baseline. Persistent real changes can then recover.
        w.previousVelocity = null; w.confidence = 0; w.goodSamples = 0;
        w.reason = 'acceleration-outlier'; continue;
      }
      if (delta !== 0) { w.lastMotionMs = timestampMs; w.stuckEvidenceMm = 0; }
      velocities[i] = velocity; deltas[i] = delta;
      w.goodSamples++; w.confidence = Math.min(1, w.goodSamples / this.recoverySamples);
      w.reason = 'measured';
    }

    // A frozen count can be suspected only when there is evidence of motion:
    // an independently moving same-side peer or an optional fresh command.
    for (const [a, b] of SIDES) for (const [i, peer] of [[a,b], [b,a]]) {
      const w = this.wheels[i];
      if (velocities[i] === null || deltas[i] !== 0) continue;
      const peerSpeed = velocities[peer] ?? 0;
      const expectedSpeed = Math.max(Math.abs(peerSpeed), Math.abs(commandedWheelMmS?.[i] ?? 0));
      w.stuckEvidenceMm += expectedSpeed * dt;
      if (timestampMs - w.lastMotionMs >= this.stuckAfterMs &&
        w.stuckEvidenceMm >= this.minimumStuckTicks * this.wheelCalibration[i].mmPerTick) {
        velocities[i] = null; deltas[i] = null;
        // Do not rebase a stationary counter every pass: continue checking it
        // so genuine new pulses recover immediately, with confidence ramped.
        w.confidence = 0; w.goodSamples = 0; w.previousVelocity = null;
        w.reason = 'motion-unconfirmed';
      }
    }

    let ambiguous = false;
    const sideDelta = [null,null], sideMasks = [0,0];
    for (let side = 0; side < 2; side++) {
      const [a,b] = SIDES[side];
      if (velocities[a] !== null && velocities[b] !== null) {
        const quantization = (this.wheelCalibration[a].mmPerTick + this.wheelCalibration[b].mmPerTick) / dt;
        const tolerance = this.pairToleranceMmS + quantization +
          this.pairRelativeTolerance * Math.max(Math.abs(velocities[a]), Math.abs(velocities[b]));
        if (Math.abs(velocities[a] - velocities[b]) > tolerance) {
          // Two sensors do not constitute a majority. Only use history to
          // isolate an abrupt outlier when its peer is markedly more plausible.
          const ea = priorVelocity[a] === null ? null : Math.abs(velocities[a] - priorVelocity[a]);
          const eb = priorVelocity[b] === null ? null : Math.abs(velocities[b] - priorVelocity[b]);
          if (ea !== null && eb !== null && Math.abs(ea-eb) > tolerance) {
            const bad = ea > eb ? a : b;
            velocities[bad] = null; deltas[bad] = null;
            this.wheels[bad].confidence = 0; this.wheels[bad].goodSamples = 0;
            this.wheels[bad].previousVelocity = null; this.wheels[bad].reason = 'pair-outlier';
          } else {
            ambiguous = true;
            for (const i of [a,b]) {
              this.wheels[i].confidence = Math.min(0.25, this.wheels[i].confidence);
              this.wheels[i].reason = 'pair-ambiguous';
            }
          }
        }
      }
      const usable = [a,b].filter(i => velocities[i] !== null);
      if (usable.length) {
        const weight = usable.reduce((s,i) => s + this.wheels[i].confidence, 0);
        sideDelta[side] = usable.reduce((s,i) => s + deltas[i] * this.wheels[i].confidence, 0) / weight;
        sideMasks[side] = usable.reduce((mask,i) => mask | (1 << i), 0);
      }
    }
    let acceptedMask = 0;
    for (let i = 0; i < 4; i++) if (velocities[i] !== null) {
      acceptedMask |= 1 << i; this.wheels[i].previousVelocity = velocities[i];
      this.wheelTravelMm[i] += deltas[i];
    }
    const observable = sideDelta.every(d => d !== null);
    if (sideDelta[0] !== null) this.leftDistanceMm += sideDelta[0];
    if (sideDelta[1] !== null) this.rightDistanceMm += sideDelta[1];
    if (observable) {
      const forward = (sideDelta[0] + sideDelta[1]) / 2;
      const angle = (sideDelta[1] - sideDelta[0]) / this.trackWidthMm;
      // SE(2) exponential: unlike endpoint Euler integration, exact for a
      // constant-curvature interval. Series avoids cancellation near zero.
      const sinc = Math.abs(angle) < 1e-5 ? 1 - angle*angle/6 : Math.sin(angle)/angle;
      const cosc = Math.abs(angle) < 1e-5 ? angle/2 - angle**3/24 : (1-Math.cos(angle))/angle;
      const dx = forward*sinc, dy = forward*cosc;
      this.xMm += Math.cos(this.headingRad)*dx - Math.sin(this.headingRad)*dy;
      this.yMm += Math.sin(this.headingRad)*dx + Math.cos(this.headingRad)*dy;
      this.headingRad += angle; this.pathLengthMm += Math.abs(forward);
    } else this.pathComplete = false;
    this.timestampMs = timestampMs;
    return this.snapshot({reason:!observable ? 'side-unobservable' : ambiguous ? 'pair-ambiguous' :
      acceptedMask !== 15 ? 'partial-feedback' : 'measured', observable, integrated:observable,
      validMask:acceptedMask, usedMask:sideMasks[0] | sideMasks[1], ambiguous,
      deltaMm:sideDelta.map(d => d ?? 0), wheelVelocityMmS:velocities, dt});
  }
}

export default RobustOdometry;
