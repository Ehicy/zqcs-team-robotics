from pathlib import Path
import shutil,re
p=Path(__file__).parent
src=p.parent/'flip-arm-sw-v2'
for f in src.glob('*.SLDPRT'):
    dest=p/f.name.replace('V2_','V3_')
    if not dest.exists(): shutil.copy2(f,dest)
s=(src/'build.cs').read_text(encoding='utf-8-sig').replace('FlipArmV2Builder','FlipArmV3Builder').replace('V2_','V3_')
s=s.replace(' static void Cut(string name)', ' static void OffsetBoss(double depth,double start,string name){End();var f=doc.FeatureManager.FeatureExtrusion2(true,false,false,0,0,m(depth),0,false,false,false,false,0,0,false,false,false,false,true,true,true,3,m(start),false);if(f==null)throw new Exception("Offset boss failed "+name);f.Name=name;}\n static void Cut(string name)')
def method(name,body):
    global s
    start=s.index(' static void '+name+'()')
    end=s.index('\n static ',start+1)
    s=s[:start]+body+s[end:]
method('Servo', ''' static void Servo(){
 New("V3_REF_MG995_envelope");Sketch(0,"");Rect(-30.5,-10,10,10);Boss(35,"MG995_case_photo_approx35");
 Sketch(0,"");Rect(-37.5,-10,17,10);OffsetBoss(3,25,"Mounting_ears_APPROX54p5");
 Sketch(0,"");Circle(0,0,9.5);Boss(38,"Raised_cover_APPROX");
 Sketch(0,"");Circle(0,0,3);Boss(42,"Output_shaft_envelope_NO_SPLINE");
 Sketch(0,"");foreach(double x in new[]{-34.0,13.0})foreach(double y in new[]{-5.0,5.0})Circle(x,y,2);Cut("Ear_holes_REFERENCE_47x10");
 Save("V3_REF_MG995_envelope",.16,.17,.19);
 }
 static void SmallServo(){
 New("V3_REF_SG90");Sketch(0,"");Rect(-11.5,-6,11.5,6);Boss(22,"SG90_case_PHOTO_APPROX23x12x22");
 Sketch(0,"");Rect(-16,-6,16,6);OffsetBoss(2,16,"SG90_ears_APPROX32");
 Sketch(0,"");Circle(-5.5,0,5.5);Boss(26,"SG90_top_boss_APPROX");
 Sketch(0,"");Circle(-5.5,0,2.4);Boss(29,"SG90_shaft_envelope_NO_SPLINE");
 Sketch(0,"");Circle(-14,0,1.1);Circle(14,0,1.1);Cut("SG90_ear_holes_REFERENCE28");
 Save("V3_REF_SG90",.08,.30,.85);
 }
 static void SmallMount(){
 New("V3_08_SG90_mount");Sketch(0,"");Rect(-21,-12,21,12);Rect(-11.9,-6.4,11.9,6.4);Boss(3,"SG90_window23p8x12p8");
 Sketch(0,"");foreach(double x in new[]{-14.0,14.0})Rect(x-1.2,-2,x+1.2,2);Cut("SG90_mount_slots_REFERENCE");
 Sketch(0,"");Circle(-6,-9,1.7);Circle(6,-9,1.7);Cut("Adapter_M3_pair12_REFERENCE");
 Save("V3_08_SG90_mount",.18,.48,.70);
 }''')
# MG995 front datum is at case rear Z=0; mirrored assembly places output toward coupler.
s=s.replace('Add(a,"V3_REF_MG995_envelope",px,py,49,id);','Add(a,"V3_REF_MG995_envelope",px,py,87,mirror);')
s=s.replace('Add(a,"V3_REF_gripper_body",ex-10*Math.Cos(gt)+10*Math.Sin(gt),ey-10*Math.Sin(gt)-10*Math.Cos(gt),-29,Rz(gripAngle));', '''Add(a,"V3_REF_SG90",ex,ey,-40,Rz(gripAngle));
  Add(a,"V3_08_SG90_mount",ex,ey,-27,Rz(gripAngle));''')
s=s.replace('px,40,48,id','px,40,53,id')
s=s.replace('new[]{-34.0,-6.0}','new[]{-14.0,14.0}').replace('new[]{31.0,49.0}','new[]{57.0,66.0}')
s=s.replace('Boss(24,"Servo_foot")','Boss(17,"Servo_foot")').replace('new[]{3.0,21.0}','new[]{4.0,13.0}')
# Run only changed parts and all assemblies, retaining the existing chassis/bin/arm geometry.
old='Base();Tower();Retainer();Arm();Wrist();Coupler();ServoStand();Shaft();Bearing();Servo();References();'
new='''foreach(string path in Directory.GetFiles(dir,"*.SLDPRT")){
 string name=Path.GetFileNameWithoutExtension(path);if(name=="V3_01_base"||name=="V3_07_servo_stand"||name=="V3_REF_MG995_envelope"||name=="V3_REF_SG90"||name=="V3_08_SG90_mount")continue;
 int er=0,wr=0;var loaded=(IModelDoc2)sw.OpenDoc6(path,1,1,"",ref er,ref wr);if(loaded==null)throw new Exception("Open failed "+name+" "+er);paths[name]=path;
 }Base();ServoStand();Servo();SmallServo();SmallMount();'''
s=s.replace(old,new).replace('FlipArm_V2_','FlipArm_V3_')
s=s.replace('SmallMount();Assembly(', 'SmallMount();BinRelief();Assembly(')
s=s.replace(' public static void Run(', ''' static void BinRelief(){doc=(IModelDoc2)sw.GetOpenDocumentByName(paths["V3_bin_180x143x80"]);if(doc.FeatureByName("Servo_ears_relief_v3")==null){Sketch(0,"");Rect(-1,10.5,10,27.5);Pocket(58,"Servo_ears_relief_v3");}Save("V3_bin_180x143x80",.78,.80,.84);}
 public static void Run(''')
s=s.replace('paths[name]=p;', 'paths[name]=p; for(IFeature vf=(IFeature)doc.FirstFeature();vf!=null;vf=(IFeature)vf.GetNextFeature()){bool vw=false;int ve=vf.GetErrorCode2(out vw);if(ve!=0)throw new Exception("Feature error "+name+" "+vf.Name+" "+ve);}Info("Feature errors=0 "+name);doc.SaveBMP(Path.Combine(dir,name+".bmp"),1000,800);')
(p/'build.cs').write_text(s,encoding='utf-8-sig')
(p/'build.ps1').write_text((src/'build.ps1').read_text(encoding='utf-8-sig').replace('FlipArmV2Builder','FlipArmV3Builder'),encoding='utf-8-sig')
