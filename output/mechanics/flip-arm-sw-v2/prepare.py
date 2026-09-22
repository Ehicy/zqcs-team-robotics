from pathlib import Path
p=Path(__file__).parent
s=(p.parent/'flip-arm-sw-v1/build.cs').read_text(encoding='utf-8-sig')
s=s.replace('FlipArmBuilder','FlipArmV2Builder').replace('const double ArmLength=130, PivotHeight=60;', 'const double ArmLength=130, PivotHeight=70;\n const double ChassisTop=35, AxisGround=110, BinFloor=68, BinDepth=80;')
# Unique file names avoid collisions with an open v1 document.
import re
s=re.sub(r'"((?:0[1-7]_|REF_)[^"]*)"',r'"V2_\1"',s)
s=s.replace('22,74','22,84').replace('height60','height70')
s=s.replace('43,71','43,81').replace('49.5,31.2,70.5','59.5,31.2,80.5').replace('55.0,65.0','65.0,75.0')
s=s.replace('return c;}','c.Select4(false,null,false);a.FixComponent();return c;}')
start=s.index(' static void Assembly()')
end=s.index(' public static void Run(',start)
s=s[:start]+'''
 static void Box(string name,double x,double y,double z,double r,double g,double b){New(name);Sketch(0,"");Rect(0,0,x,y);Boss(z,name);Save(name,r,g,b);}
 static void References(){
  Box("V2_REF_chassis_255p8x149",255.8,3,149,.12,.38,.55);
  Box("V2_REF_electronics_envelope",150,25,90,.15,.55,.28);
  New("V2_bin_180x143x80");Sketch(0,"");Rect(0,0,186,149);Boss(3,"Floor_T3");
  Sketch(0,"");Rect(0,0,186,149);Rect(3,3,183,146);Boss(83,"Walls_T3_depth80");Save("V2_bin_180x143x80",.78,.80,.84);
  New("V2_REF_wheel_D60");Sketch(0,"");Circle(0,0,30);Boss(28,"Wheel_width28_PLACEHOLDER");Save("V2_REF_wheel_D60",.12,.12,.12);
  Box("V2_REF_gripper_body",20,20,58,.22,.25,.30);
  Box("V2_REF_gripper_finger",35,14,4,.90,.64,.12);
  Box("V2_REF_block40",40,40,40,.95,.80,.08);
 }
 static void Assembly(double angle,string pose){
  doc=(IModelDoc2)sw.NewDocument(Template(2),0,0,0);IAssemblyDoc a=(IAssemblyDoc)doc;
  double[] id=Rz(0),mirror=new double[]{-1,0,0,0,1,0,0,0,-1};double px=35,py=AxisGround;
  Add(a,"V2_REF_chassis_255p8x149",0,ChassisTop-3,-74.5,id);
  foreach(double wx in new[]{42.0,213.0}) {Add(a,"V2_REF_wheel_D60",wx,30,-102.5,id);Add(a,"V2_REF_wheel_D60",wx,30,74.5,id);}
  Add(a,"V2_REF_electronics_envelope",87,ChassisTop,-45,id);
  Add(a,"V2_bin_180x143x80",69.8,BinFloor-3,74.5,new double[]{1,0,0,0,0,-1,0,1,0});
  Add(a,"V2_01_base",px,ChassisTop+5,0,new double[]{1,0,0,0,0,1,0,-1,0});
  Add(a,"V2_02_bearing_tower",px,40,-26,id);Add(a,"V2_02_bearing_tower",px,40,26,mirror);
  Add(a,"V2_03_bearing_retainer",px,py,-28,id);Add(a,"V2_03_bearing_retainer",px,py,28,mirror);
  Add(a,"V2_REF_625ZZ",px,py,-26,id);Add(a,"V2_REF_625ZZ",px,py,26,mirror);
  Add(a,"V2_REF_5mm_shaft",px,py,-27,id);
  double t=angle*Math.PI/180;double[] ar=Rz(angle),right={Math.Cos(t),Math.Sin(t),0,Math.Sin(t),-Math.Cos(t),0,0,0,-1};
  Add(a,"V2_04_arm_plate",px,py,-16,ar);Add(a,"V2_04_arm_plate",px,py,16,right);
  double ex=px+ArmLength*Math.Cos(t),ey=py+ArmLength*Math.Sin(t);
  Add(a,"V2_05_wrist_adapter",ex,ey,-10,ar);
  Add(a,"V2_06_horn_coupler",px,py,45,mirror);
  Add(a,"V2_07_servo_stand",px,40,48,id);
  Add(a,"V2_REF_MG995_envelope",px,py,49,id);
  // Gripper is a concept envelope, not a fabricated SG90 linkage.
  double gripAngle=angle-216.86989765,gt=gripAngle*Math.PI/180;
  double cx=px+150*Math.Cos(t),cy=py+150*Math.Sin(t);
  Add(a,"V2_REF_gripper_body",ex-10*Math.Cos(gt)+10*Math.Sin(gt),ey-10*Math.Sin(gt)-10*Math.Cos(gt),-29,Rz(gripAngle));
  foreach(double gz in new[]{-25.0,21.0})Add(a,"V2_REF_gripper_finger",cx-17.5*Math.Cos(gt)+7*Math.Sin(gt),cy-17.5*Math.Sin(gt)-7*Math.Cos(gt),gz,Rz(gripAngle));
  Add(a,"V2_REF_block40",cx-20*Math.Cos(gt)+20*Math.Sin(gt),cy-20*Math.Sin(gt)-20*Math.Cos(gt),-20,Rz(gripAngle));
  Info(pose+" angle="+angle+" block_center_mm="+cx+","+cy+" bin_rim="+(BinFloor+BinDepth));
  doc.ClearSelection2(true);doc.EditRebuild3();doc.ShowNamedView2("",7);doc.ViewZoomtofit2();
  foreach(string ext in new[]{"SLDASM","STEP"}){int e=0,w=0;bool ok=doc.Extension.SaveAs(Path.Combine(dir,"FlipArm_V2_"+pose+"."+ext),0,1,null,ref e,ref w);Info(pose+" "+ext+" saved="+ok+" error="+e);if(!ok||e!=0)throw new Exception("Assembly export failed");}
  Info("Preview="+doc.SaveBMP(Path.Combine(dir,pose+".bmp"),1500,1050));
 }
''' + s[end:]
s=s.replace('Servo();Assembly();','Servo();References();Assembly(216.86989765,"pickup");Assembly(90,"raised");Assembly(60,"deposit");')
(p/'build.cs').write_text(s,encoding='utf-8-sig')
ps=(p.parent/'flip-arm-sw-v1/build.ps1').read_text(encoding='utf-8-sig').replace('FlipArmBuilder','FlipArmV2Builder')
(p/'build.ps1').write_text(ps,encoding='utf-8-sig')

