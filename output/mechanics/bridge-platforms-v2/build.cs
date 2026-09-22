using System;
using System.IO;
using SolidWorks.Interop.sldworks;
public class BridgePlatformsBuilder {
 static ISldWorks sw; static StreamWriter log;
 static void Save(IModelDoc2 doc,string path){int e=0,w=0;bool ok=doc.Extension.SaveAs(path,0,1,null,ref e,ref w);log.WriteLine(Path.GetFileName(path)+": "+ok+" error="+e+" warning="+w);log.Flush();if(!ok||e!=0)throw new Exception("Save failed "+path);}
 static void Add(IAssemblyDoc a,string path,string name,double x,double y,double z){
  var c=(IComponent2)a.AddComponent5(path,0,"",false,"",0,0,0);if(c==null)throw new Exception("Add failed "+name);
  c.Transform2=(MathTransform)((IMathUtility)sw.GetMathUtility()).CreateTransform(new double[]{1,0,0,0,1,0,0,0,1,x/1000,y/1000,z/1000,1,0,0,0});
  c.Name2=name;c.Select4(false,null,false);a.FixComponent();log.WriteLine(name+" translation mm="+x+","+y+","+z+" fixed");
 }
 public static void Run(string dir){using(log=new StreamWriter(Path.Combine(dir,"verification.txt"))){
  sw=(ISldWorks)Activator.CreateInstance(Type.GetTypeFromProgID("SldWorks.Application"));sw.Visible=true;
  var opened=(object[])sw.GetDocuments();if(opened!=null)foreach(object o in opened){var d=(IModelDoc2)o;if(d.GetPathName().StartsWith(dir,StringComparison.OrdinalIgnoreCase))sw.CloseDoc(d.GetTitle());}
  var part=(IModelDoc2)sw.NewDocument(@"C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2026\templates\gb_part.prtdot",0,0,0);
  var plane=(IFeature)part.FirstFeature();while(plane!=null&&plane.GetTypeName2()!="RefPlane")plane=(IFeature)plane.GetNextFeature();plane.Select2(false,0);
  part.SketchManager.InsertSketch(true);part.SketchManager.CreateCornerRectangle(0,0,0,.3,.3,0);part.SketchManager.InsertSketch(true);
  var f=part.FeatureManager.FeatureExtrusion2(true,false,false,0,0,.3,0,false,false,false,false,0,0,false,false,false,false,true,true,true,0,0,false);
  if(f==null)throw new Exception("Platform extrusion failed");f.Name="Platform_300x300x300";
  part.MaterialPropertyValues=new double[]{.78,.57,.18,.3,.7,.2,.25,0,0};part.EditRebuild3();
  var bodies=(object[])((IPartDoc)part).GetBodies2(0,false);if(bodies==null||bodies.Length!=1)throw new Exception("Platform not one solid");
  bool warn;int err=f.GetErrorCode2(out warn);log.WriteLine("Platform solids=1 feature error="+err);if(err!=0)throw new Exception("Feature error");
  string platform=Path.Combine(dir,"Platform_300x300x300.SLDPRT");Save(part,platform);Save(part,Path.ChangeExtension(platform,"STEP"));
  string source=Path.GetFullPath(Path.Combine(dir,@"..\bridge-test-v1\Bridge_TEST_300x150_R15_T10.SLDPRT"));
  string bridge=Path.Combine(dir,"Bridge_segment_for_platforms.SLDPRT");File.Copy(source,bridge,true);
  int e=0,w=0;var bp=(IModelDoc2)sw.OpenDoc6(bridge,1,1,"",ref e,ref w);if(bp==null)throw new Exception("Bridge open "+e);
  var doc=(IModelDoc2)sw.NewDocument(@"C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2026\templates\gb_assembly.asmdot",0,0,0);var a=(IAssemblyDoc)doc;
  Add(a,platform,"Entry_platform",-300,0,-75);Add(a,bridge,"Test_bridge_1",150,175,0);Add(a,bridge,"Test_bridge_2",450,175,0);Add(a,platform,"Exit_platform",600,0,-75);
  doc.ClearSelection2(true);doc.EditRebuild3();doc.ShowNamedView2("",7);doc.ViewZoomtofit2();
  object[] components=(object[])a.GetComponents(false);log.WriteLine("Components="+components.Length);if(components.Length!=4)throw new Exception("Component count mismatch");
  Save(doc,Path.Combine(dir,"Bridge_with_platforms_TEST.SLDASM"));Save(doc,Path.Combine(dir,"Bridge_with_platforms_TEST.STEP"));
  log.WriteLine("Preview="+doc.SaveBMP(Path.Combine(dir,"preview.bmp"),1500,950));
  log.WriteLine("Design envelope mm: L1200 W300 H315. Bridge ends Y300, crowns Y315. Two test arcs, not official clearance geometry.");
 }Console.WriteLine(File.ReadAllText(Path.Combine(dir,"verification.txt")));}
}


