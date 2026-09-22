using System;
using System.IO;
using System.Collections.Generic;
using SolidWorks.Interop.sldworks;

// Millimetres in design inputs; SolidWorks API uses metres.
// Preliminary geometry, not a released manufacturing design.
public class FlipArmV3Builder {
 static ISldWorks sw; static IModelDoc2 doc; static string dir;
 static StreamWriter log; static Dictionary<string,string> paths = new Dictionary<string,string>();
 const double ArmLength=130, PivotHeight=70;
 const double ChassisTop=35, AxisGround=110, BinFloor=68, BinDepth=80;
 static double m(double v){return v/1000.0;}
 static void Info(string s){log.WriteLine(s);log.Flush();Console.WriteLine(s);}
 static IFeature Plane(int index){IFeature f=(IFeature)doc.FirstFeature();int n=0;while(f!=null){if(f.GetTypeName2()=="RefPlane"){if(n++==index)return f;} f=(IFeature)f.GetNextFeature();}throw new Exception("Reference plane not found");}
 static void Sketch(int plane,string name){doc.ClearSelection2(true);Plane(plane).Select2(false,0);doc.SketchManager.InsertSketch(true);doc.SketchManager.AddToDB=true;}
 static void Rect(double x1,double y1,double x2,double y2){doc.SketchManager.CreateCornerRectangle(m(x1),m(y1),0,m(x2),m(y2),0);}
 static void Circle(double x,double y,double r){doc.SketchManager.CreateCircleByRadius(m(x),m(y),0,m(r));}
 static void End(){doc.SketchManager.AddToDB=false;doc.SketchManager.InsertSketch(true);}
 static void Boss(double thickness,string name){End();IFeature f=doc.FeatureManager.FeatureExtrusion2(true,false,false,0,0,m(thickness),0,false,false,false,false,0,0,false,false,false,false,true,true,true,0,0,false);if(f==null)throw new Exception("Extrusion failed: "+name);f.Name=name;}
 static void OffsetBoss(double depth,double start,string name){End();var f=doc.FeatureManager.FeatureExtrusion2(true,false,false,0,0,m(depth),0,false,false,false,false,0,0,false,false,false,false,true,true,true,3,m(start),false);if(f==null)throw new Exception("Offset boss failed "+name);f.Name=name;}
 static void Cut(string name){End();IFeature f=doc.FeatureManager.FeatureCut3(false,false,false,1,1,0.1,0.1,false,false,false,false,0,0,false,false,false,false,false,true,true,true,true,false,0,0,false);if(f==null)throw new Exception("Cut failed: "+name);f.Name=name;}
 static void Pocket(double depth,string name){End();IFeature f=doc.FeatureManager.FeatureCut3(true,false,true,0,0,m(depth),0,false,false,false,false,0,0,false,false,false,false,false,true,true,true,true,false,0,0,false);if(f==null)throw new Exception("Pocket failed: "+name);f.Name=name;}
 static string Template(int type){string t=sw.GetDocumentTemplate(type,"",0,0,0);if(File.Exists(t))return t;return type==1?@"C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2026\templates\gb_part.prtdot":@"C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2026\templates\gb_assembly.asmdot";}
 static void New(string name){string t=Template(1);doc=(IModelDoc2)sw.NewDocument(t,0,0,0);if(doc==null)throw new Exception("New part failed: "+t);Info("BUILD "+name);}
 static void Save(string name,double r,double g,double b){doc.ClearSelection2(true);doc.EditRebuild3();doc.MaterialPropertyValues=new double[]{r,g,b,0.3,0.7,0.2,0.25,0,0};doc.ShowNamedView2("",7);doc.ViewZoomtofit2();int e=0,w=0;string p=Path.Combine(dir,name+".SLDPRT");bool ok=doc.Extension.SaveAs(p,0,1,null,ref e,ref w);if(!ok||e!=0)throw new Exception("Save failed "+name+" "+e);paths[name]=p; for(IFeature vf=(IFeature)doc.FirstFeature();vf!=null;vf=(IFeature)vf.GetNextFeature()){bool vw=false;int ve=vf.GetErrorCode2(out vw);if(ve!=0)throw new Exception("Feature error "+name+" "+vf.Name+" "+ve);}Info("Feature errors=0 "+name);doc.SaveBMP(Path.Combine(dir,name+".bmp"),1000,800);
  object[] bodies=(object[])((IPartDoc)doc).GetBodies2(0,false);double[] box=(double[])((IPartDoc)doc).GetPartBox(true);Info(name+" solidBodies="+(bodies==null?0:bodies.Length)+" box_m="+string.Join(",",box));if(bodies==null||bodies.Length!=1)throw new Exception("Expected one solid: "+name);
  e=0;w=0;ok=doc.Extension.SaveAs(Path.Combine(dir,name+".STEP"),0,1,null,ref e,ref w);Info("STEP "+name+" ok="+ok+" error="+e);if(!ok||e!=0)throw new Exception("STEP export failed");
 }
 // Converts a point on a model reference plane into sketch coordinates.
 static void Hole3(double x,double y,double z,double radius){IMathUtility u=(IMathUtility)sw.GetMathUtility();IMathPoint p=(IMathPoint)u.CreatePoint(new double[]{m(x),m(y),m(z)});ISketch sk=(ISketch)doc.SketchManager.ActiveSketch;p=(IMathPoint)p.MultiplyTransform(sk.ModelToSketchTransform);double[] q=(double[])p.ArrayData;doc.SketchManager.CreateCircleByRadius(q[0],q[1],0,m(radius));}
 static void Base(){New("V3_01_base");Sketch(0,"");Rect(-35,-70,35,70);Boss(5,"Base_70x140x5");Sketch(0,"");foreach(double x in new[]{-26.0,26.0})foreach(double y in new[]{-51.0,51.0}){Rect(x-1.7,y-6,x+1.7,y+6);}Cut("Chassis_adjustment_slots_UNVERIFIED_positions");Sketch(0,"");foreach(double x in new[]{-20.0,20.0})foreach(double y in new[]{-14.0,14.0})Circle(x,y,1.7);foreach(double x in new[]{-9.0,31.0})foreach(double y in new[]{57.0,66.0})Circle(x,y,1.7);Cut("Support_and_servo_mount_M3_clearance");Save("V3_01_base",.30,.33,.38);}
 static void Tower(){New("V3_02_bearing_tower");Sketch(0,"");Rect(-28,0,28,8);Boss(24,"Foot_56x24x8");Sketch(0,"");Rect(-22,0,22,84);Boss(8,"Tower_pivot_height70");Sketch(0,"");Circle(0,PivotHeight,5.8);Circle(-14,PivotHeight,1.7);Circle(14,PivotHeight,1.7);Cut("Shaft_access_and_retainer_M3");Sketch(0,"");Circle(0,PivotHeight,8.1);Pocket(5.2,"625ZZ_pocket_D16p2_depth5p2_TEST_FIT");Sketch(1,"");Hole3(-20,0,12,1.7);Hole3(20,0,12,1.7);Cut("Foot_M3_through");Save("V3_02_bearing_tower",.1,.42,.70);}
 static void Retainer(){New("V3_03_bearing_retainer");Sketch(0,"");Rect(-20,-12,20,12);Circle(0,0,5.8);Circle(-14,0,1.7);Circle(14,0,1.7);Boss(2,"Bearing_retainer_40x24x2");Save("V3_03_bearing_retainer",.2,.3,.42);}
 static void Arm(){New("V3_04_arm_plate");Sketch(0,"");Rect(0,-11,ArmLength,11);Boss(6,"Arm_web_L130_W22_T6");Sketch(0,"");Circle(0,0,14);Boss(12,"Split_clamp_hub");Sketch(0,"");Circle(ArmLength,0,16);Boss(6,"Wrist_end_R16");Sketch(0,"");Circle(0,0,2.6);Circle(ArmLength-6,0,1.7);Circle(ArmLength+6,0,1.7);Cut("Shaft_D5p2_and_wrist_M3");Sketch(0,"");Rect(-.6,2.2,.6,15);Cut("Clamp_split_1p2");Sketch(2,"");Hole3(0,9,8.5,1.7);Cut("Clamp_crossbolt_M3");Save("V3_04_arm_plate",.94,.53,.13);}
 static void Wrist(){New("V3_05_wrist_adapter");Sketch(0,"");Rect(-12,-12,12,12);Circle(-6,0,1.7);Circle(6,0,1.7);Boss(20,"Wrist_spacer_W20");Sketch(2,"");foreach(double y in new[]{-7.0,7.0})foreach(double z in new[]{5.0,15.0})Hole3(0,y,z,1.7);Cut("Gripper_adapter_M3_14x10_NOT_SG90_DIRECT");Save("V3_05_wrist_adapter",.95,.65,.2);}
 static void Coupler(){New("V3_06_horn_coupler");Sketch(0,"");Circle(0,0,19);Boss(4,"Horn_flange_D38");Sketch(0,"");Circle(0,0,14);Boss(16,"Shaft_clamp_hub");Sketch(0,"");Circle(0,0,2.6);Cut("Shaft_D5p2");Sketch(0,"");Rect(-.6,2.2,.6,20);Cut("Clamp_split");Sketch(0,"");Rect(-10.5,-1.3,-6,1.3);Rect(6,-1.3,10.5,1.3);Cut("Horn_two_adjustment_slots_VERIFY_HORN");Sketch(2,"");Hole3(0,9,10,1.7);Cut("Coupler_clamp_M3");Save("V3_06_horn_coupler",.85,.38,.12);}
 static void ServoStand(){New("V3_07_servo_stand");Sketch(0,"");Rect(-21,0,43,8);Boss(17,"Servo_foot");Sketch(0,"");Rect(-21,0,43,81);Boss(6,"Servo_face_plate");Sketch(0,"");Rect(-10.5,59.5,31.2,80.5);Cut("MG995_body_window_REFERENCE");Sketch(0,"");foreach(double x in new[]{-13.6,33.6})foreach(double y in new[]{65.0,75.0})Rect(x-2,y-2.5,x+2,y+2.5);Cut("MG995_47p2x10_reference_slots_VERIFY");Sketch(1,"");foreach(double x in new[]{-9.0,31.0})foreach(double z in new[]{4.0,13.0})Hole3(x,0,z,1.7);Cut("Servo_foot_M3");Save("V3_07_servo_stand",.12,.43,.7);}
 static void Shaft(){New("V3_REF_5mm_shaft");Sketch(0,"");Circle(0,0,2.5);Boss(68,"Purchased_steel_shaft_D5_L68_NOT_PRINT");Save("V3_REF_5mm_shaft",.65,.66,.68);}
 static void Bearing(){New("V3_REF_625ZZ");Sketch(0,"");Circle(0,0,8);Circle(0,0,2.5);Boss(5,"Purchased_625ZZ_5x16x5_NOT_PRINT");Save("V3_REF_625ZZ",.65,.66,.68);}
 static void Servo(){
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
 }
 static IComponent2 Add(IAssemblyDoc a,string name,double x,double y,double z,double[] rotation){IComponent2 c=(IComponent2)a.AddComponent5(paths[name],0,"",false,"",0,0,0);if(c==null)throw new Exception("Component failed "+name);IMathUtility u=(IMathUtility)sw.GetMathUtility();double[] data=new double[16];Array.Copy(rotation,data,9);data[9]=m(x);data[10]=m(y);data[11]=m(z);data[12]=1;c.Transform2=(MathTransform)u.CreateTransform(data);c.Select4(false,null,false);a.FixComponent();return c;}
 static double[] Rz(double degrees){double t=degrees*Math.PI/180;return new double[]{Math.Cos(t),Math.Sin(t),0,-Math.Sin(t),Math.Cos(t),0,0,0,1};}

 static void Box(string name,double x,double y,double z,double r,double g,double b){New(name);Sketch(0,"");Rect(0,0,x,y);Boss(z,name);Save(name,r,g,b);}
 static void References(){
  Box("V3_REF_chassis_255p8x149",255.8,3,149,.12,.38,.55);
  Box("V3_REF_electronics_envelope",150,25,90,.15,.55,.28);
  New("V3_bin_180x143x80");Sketch(0,"");Rect(0,0,186,149);Boss(3,"Floor_T3");
  Sketch(0,"");Rect(0,0,186,149);Rect(3,3,183,146);Boss(83,"Walls_T3_depth80");Sketch(0,"");Rect(-1,19.5,10,27.5);Pocket(58,"Servo_stand_relief_W8_H58");Save("V3_bin_180x143x80",.78,.80,.84);
  New("V3_REF_wheel_D60");Sketch(0,"");Circle(0,0,30);Boss(28,"Wheel_width28_PLACEHOLDER");Save("V3_REF_wheel_D60",.12,.12,.12);
  Box("V3_REF_gripper_body",20,20,58,.22,.25,.30);
  Box("V3_REF_gripper_finger",35,14,4,.90,.64,.12);
  Box("V3_REF_block40",40,40,40,.95,.80,.08);
 }
 static void Assembly(double angle,string pose){
  doc=(IModelDoc2)sw.NewDocument(Template(2),0,0,0);IAssemblyDoc a=(IAssemblyDoc)doc;
  double[] id=Rz(0),mirror=new double[]{-1,0,0,0,1,0,0,0,-1};double px=35,py=AxisGround;
  Add(a,"V3_REF_chassis_255p8x149",0,ChassisTop-3,-74.5,id);
  foreach(double wx in new[]{42.0,213.0}) {Add(a,"V3_REF_wheel_D60",wx,30,-102.5,id);Add(a,"V3_REF_wheel_D60",wx,30,74.5,id);}
  Add(a,"V3_REF_electronics_envelope",87,ChassisTop,-45,id);
  Add(a,"V3_bin_180x143x80",69.8,BinFloor-3,74.5,new double[]{1,0,0,0,0,-1,0,1,0});
  Add(a,"V3_01_base",px,ChassisTop+5,0,new double[]{1,0,0,0,0,1,0,-1,0});
  Add(a,"V3_02_bearing_tower",px,40,-26,id);Add(a,"V3_02_bearing_tower",px,40,26,mirror);
  Add(a,"V3_03_bearing_retainer",px,py,-28,id);Add(a,"V3_03_bearing_retainer",px,py,28,mirror);
  Add(a,"V3_REF_625ZZ",px,py,-26,id);Add(a,"V3_REF_625ZZ",px,py,26,mirror);
  Add(a,"V3_REF_5mm_shaft",px,py,-27,id);
  double t=angle*Math.PI/180;double[] ar=Rz(angle),right={Math.Cos(t),Math.Sin(t),0,Math.Sin(t),-Math.Cos(t),0,0,0,-1};
  Add(a,"V3_04_arm_plate",px,py,-16,ar);Add(a,"V3_04_arm_plate",px,py,16,right);
  double ex=px+ArmLength*Math.Cos(t),ey=py+ArmLength*Math.Sin(t);
  Add(a,"V3_05_wrist_adapter",ex,ey,-10,ar);
  Add(a,"V3_06_horn_coupler",px,py,45,mirror);
  Add(a,"V3_07_servo_stand",px,40,53,id);
  Add(a,"V3_REF_MG995_envelope",px,py,87,mirror);
  // Gripper is a concept envelope, not a fabricated SG90 linkage.
  double gripAngle=angle-216.86989765,gt=gripAngle*Math.PI/180;
  double cx=px+150*Math.Cos(t),cy=py+150*Math.Sin(t);
  Add(a,"V3_REF_SG90",ex,ey,-40,Rz(gripAngle));
  Add(a,"V3_08_SG90_mount",ex,ey,-27,Rz(gripAngle));
  foreach(double gz in new[]{-25.0,21.0})Add(a,"V3_REF_gripper_finger",cx-17.5*Math.Cos(gt)+7*Math.Sin(gt),cy-17.5*Math.Sin(gt)-7*Math.Cos(gt),gz,Rz(gripAngle));
  Add(a,"V3_REF_block40",cx-20*Math.Cos(gt)+20*Math.Sin(gt),cy-20*Math.Sin(gt)-20*Math.Cos(gt),-20,Rz(gripAngle));
  Info(pose+" angle="+angle+" block_center_mm="+cx+","+cy+" bin_rim="+(BinFloor+BinDepth));
  doc.ClearSelection2(true);doc.EditRebuild3();doc.ShowNamedView2("",7);doc.ViewZoomtofit2();
  foreach(string ext in new[]{"SLDASM","STEP"}){int e=0,w=0;bool ok=doc.Extension.SaveAs(Path.Combine(dir,"FlipArm_V3_"+pose+"."+ext),0,1,null,ref e,ref w);Info(pose+" "+ext+" saved="+ok+" error="+e);if(!ok||e!=0)throw new Exception("Assembly export failed");}
  Info("Preview="+doc.SaveBMP(Path.Combine(dir,pose+".bmp"),1500,1050));
 }
 static void BinRelief(){doc=(IModelDoc2)sw.GetOpenDocumentByName(paths["V3_bin_180x143x80"]);if(doc.FeatureByName("Servo_ears_relief_v3")==null){Sketch(0,"");Rect(-1,10.5,10,27.5);Pocket(58,"Servo_ears_relief_v3");}Save("V3_bin_180x143x80",.78,.80,.84);}
 public static void Run(string output){dir=output;log=new StreamWriter(Path.Combine(dir,"build-log.txt"));try{sw=(ISldWorks)Activator.CreateInstance(Type.GetTypeFromProgID("SldWorks.Application"));sw.Visible=true;Info("SolidWorks "+sw.RevisionNumber());object[] opened=(object[])sw.GetDocuments();if(opened!=null)foreach(object o in opened){IModelDoc2 old=(IModelDoc2)o;if(old.GetPathName().StartsWith(dir,StringComparison.OrdinalIgnoreCase))sw.CloseDoc(old.GetTitle());}foreach(string path in Directory.GetFiles(dir,"*.SLDPRT")){
 string name=Path.GetFileNameWithoutExtension(path);if(name=="V3_01_base"||name=="V3_07_servo_stand"||name=="V3_REF_MG995_envelope"||name=="V3_REF_SG90"||name=="V3_08_SG90_mount")continue;
 int er=0,wr=0;var loaded=(IModelDoc2)sw.OpenDoc6(path,1,1,"",ref er,ref wr);if(loaded==null)throw new Exception("Open failed "+name+" "+er);paths[name]=path;
 }Base();ServoStand();Servo();SmallServo();SmallMount();BinRelief();Assembly(216.86989765,"pickup");Assembly(90,"raised");Assembly(60,"deposit");Info("DONE - preliminary layout; not motion-mated or manufacturing released");}finally{log.Dispose();}}
}

