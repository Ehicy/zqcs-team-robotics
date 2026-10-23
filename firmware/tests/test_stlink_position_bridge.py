"""Offline tests only: channel isolation, cancellation, and host watchdog."""
import sys,time,unittest,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from stlink_position_bridge import PositionBridge,CHANNELS

class FakeBridge(PositionBridge):
    def __init__(self):
        super().__init__(start_worker=False)
        self.writes=[]
        self.registers=[0,0,1500]
        self.after_write=None
    def open(self):
        self.connected=True
        self.actual=self.registers.copy();self.target=self.actual.copy()
        self.updated=self.last_client=time.monotonic()
    def write_pulse(self,ch,pulse):
        cfg=CHANNELS[ch]
        assert pulse==0 or cfg['minimum']<=pulse<=cfg['maximum']
        self.owned.add(ch)
        self.writes.append((ch,pulse))
        self.actual[ch]=self.target[ch]=self.registers[ch]=pulse
        if pulse==0:self.owned.discard(ch)
        if self.after_write:
            cb,self.after_write=self.after_write,None;cb()
    def readback(self):self.updated=time.monotonic()

class Tests(unittest.TestCase):
    def setUp(self):
        self.b=FakeBridge();self.seq=0;self.send('connect')
    def send(self,action,ch=None,pulse=None,number=None):
        self.seq+=1
        self.b.submit(dict(action=action,channel=ch,pulse=pulse,id=self.seq if number is None else number))
        self.b.tick()
    def finish(self):
        for _ in range(25):
            for seq in self.b.steps:
                if seq:seq[0]=(0,*seq[0][1:])
            if not any(self.b.steps) and not self.b.pending:return
            self.b.tick()
        self.fail('sequence did not finish')
    def test_connect_no_pwm_write(self):
        self.assertEqual(self.b.writes,[]);self.assertEqual(self.b.actual,[0,0,1500])
    def test_required_channel(self):
        for ch in (None,True,-1,3,'1'):
            with self.assertRaises(ValueError):self.send('reference',ch)
        self.assertEqual(self.b.writes,[])
    def test_no_relative_move_without_reference(self):
        with self.assertRaises(ValueError):self.send('pulse',1,1500)
    def test_reference_isolated(self):
        self.send('reference',1);self.assertEqual(self.b.writes,[(1,1500)])
        self.assertEqual(self.b.actual,[0,1500,1500])
    def test_pulse_limits(self):
        self.send('reference',0)
        for p in (True,1469,1531,1520):
            with self.assertRaises(ValueError):self.send('pulse',0,p)
    def test_small_arm_range(self):
        self.send('reference',1)
        for p in (1510,1520,1530,1540,1550):self.send('pulse',1,p)
        self.assertEqual(self.b.actual,[0,1550,1500])
    def test_hold_does_not_center(self):
        self.send('reference',0);self.send('pulse',0,1505);self.send('hold',0)
        self.assertEqual(self.b.writes,[(0,1500),(0,1505)])
    def test_each_channel_test_isolated(self):
        for ch,delta in [(0,20),(1,50),(2,50)]:
            self.b.writes=[];self.send('test',ch);self.finish()
            self.assertEqual(self.b.writes,[(ch,1500),(ch,1500+delta),(ch,1500),(ch,0)])
    def test_test_from_existing_value(self):
        self.send('reference',0);self.send('pulse',0,1510);self.b.writes=[]
        self.send('test',0);self.finish()
        self.assertEqual(self.b.writes,[(0,1510),(0,1530),(0,1510),(0,0)])
    def test_stale_off_honored(self):
        self.send('reference',1);self.send('off',1,number=0)
        self.assertEqual(self.b.actual,[0,0,1500])
    def test_stale_motion_rejected(self):
        self.send('reference',1);self.send('off',1);self.send('reference',1,number=1)
        self.assertEqual(self.b.actual,[0,0,1500])
    def test_off_only_cancels_target(self):
        self.send('test',0);self.send('test',1);self.send('off',0)
        self.assertFalse(self.b.steps[0]);self.assertTrue(self.b.steps[1])
        self.finish();self.assertEqual(self.b.actual,[0,0,1500])
    def test_all_off(self):
        self.send('test',0);self.send('test',1);self.send('off_all');self.finish()
        self.assertEqual(self.b.actual,[0,0,0]);self.assertFalse(any(self.b.steps))
    def test_keepalive_does_not_enable(self):
        self.send('keepalive');self.assertEqual(self.b.writes,[])
    def test_expiry_leaves_untouched_door(self):
        self.send('reference',1);self.b.last_client=time.monotonic()-4;self.b.tick()
        self.assertEqual(self.b.actual,[0,0,1500])
    def test_expiry_without_motion_does_not_close_door(self):
        self.b.last_client=time.monotonic()-4;self.b.tick();self.assertEqual(self.b.writes,[])
    def test_hidden_page_cancel_queued_start(self):
        self.b.submit(dict(action='reference',channel=0,id=100))
        self.send('off_owned',number=101);self.finish();self.assertEqual(self.b.writes,[])
    def test_inflight_test_cannot_restart_after_off(self):
        self.b.after_write=lambda:self.b.submit(dict(action='off_owned',id=100))
        self.send('test',0);self.finish();self.assertEqual(self.b.writes,[(0,1500),(0,0)])
    def test_status_three_channels(self):
        s=self.b.status();self.assertEqual(s['schemaVersion'],2)
        self.assertEqual([c['pin'] for c in s['channels']],['PB6','PB7','PB8'])
    def test_real_writer_address_mapping(self):
        b=PositionBridge(start_worker=False);calls=[];values={}
        b.check_hold=lambda:None
        def cli(*args):
            calls.append(args);values[int(args[1],16)]=int(args[2],16)
        b.cli=cli;b.read=lambda address,size:struct.pack('<I',values[address])
        for ch in range(3):b.write_pulse(ch,1500)
        self.assertEqual([int(c[1],16) for c in calls],[0x40000834,0x40000838,0x4000083c])
    def test_cutoff_failure_does_not_skip_other_channel(self):
        self.send('reference',0);self.send('reference',1)
        old=self.b.write_pulse
        def write(ch,pulse):
            if ch==0:raise RuntimeError('failed PB6')
            old(ch,pulse)
        self.b.write_pulse=write;self.b.handle_error(RuntimeError('link fault'))
        self.assertIn((1,0),self.b.writes);self.assertIn('PB6',self.b.error)
        self.assertFalse(self.b.connected)

if __name__=='__main__':unittest.main()
