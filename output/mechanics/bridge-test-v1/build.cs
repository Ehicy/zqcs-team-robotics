using System;
using System.IO;
using SolidWorks.Interop.sldworks;

// Test surrogate, NOT an official field CAD reproduction. Inputs in mm.
public class BridgeTestBuilder {
 public const double Length=300, Width=150, Rise=15, VerticalThickness=10;
 // Interpretation only: lower arc crown is 130 above the ground datum.
 public const double UndersideCrownHeight=130;
 public static void Run(string output) {
  var sw=(ISldWorks)Activator.CreateInstance(Type.GetTypeFromProgID("SldWorks.Application"));
  sw.Visible=true;
  string template=@"C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2026\templates\gb_part.prtdot";
  var doc=(IModelDoc2)sw.NewDocument(template,0,0,0);
  if(doc==null)throw new Exception("Cannot create bridge document");
  IFeature plane=(IFeature)doc.FirstFeature();
  while(plane!=null && plane.GetTypeName2()!="RefPlane")plane=(IFeature)plane.GetNextFeature();
  if(plane==null)throw new Exception("No reference plane");
  plane.Select2(false,0);var sk=doc.SketchManager;sk.InsertSketch(true);sk.AddToDB=true;
  double a=Length/2000, b=(UndersideCrownHeight-Rise)/1000, r=Rise/1000, t=VerticalThickness/1000;
  sk.Create3PointArc(-a,b,0,a,b,0,0,b+r,0);
  sk.CreateLine(a,b,0,a,b+t,0);
  sk.Create3PointArc(a,b+t,0,-a,b+t,0,0,b+r+t,0);
  sk.CreateLine(-a,b+t,0,-a,b,0);
  sk.AddToDB=false;sk.InsertSketch(true);
  var f=doc.FeatureManager.FeatureExtrusion2(true,false,false,0,0,Width/1000,0,false,false,false,false,0,0,false,false,false,false,true,true,true,0,0,false);
  if(f==null)throw new Exception("Bridge extrusion failed");
  f.Name="TEST_L300_W150_RISE15_VERTICAL_T10";
  doc.ClearSelection2(true);doc.EditRebuild3();
  doc.MaterialPropertyValues=new double[]{.75,.79,.84,.3,.7,.2,.25,0,0};
  doc.ShowNamedView2("",7);doc.ViewZoomtofit2();
  using(var log=new StreamWriter(Path.Combine(output,"verification.txt"))){
   log.WriteLine("SolidWorks "+sw.RevisionNumber());
   var bodies=(object[])((IPartDoc)doc).GetBodies2(0,false);
   if(bodies==null||bodies.Length!=1)throw new Exception("Expected exactly one solid");
   log.WriteLine("Solid bodies: "+bodies.Length);
   bool warning=false;int featureError=f.GetErrorCode2(out warning);
   log.WriteLine("Extrusion feature error: "+featureError+" warning: "+warning);
   if(featureError!=0)throw new Exception("Feature rebuild error");
   var body=(IBody2)bodies[0];double x,y,z;
   foreach(var axis in new double[][]{new double[]{1,0,0},new double[]{-1,0,0},new double[]{0,1,0},new double[]{0,-1,0},new double[]{0,0,1},new double[]{0,0,-1}}){
    body.GetExtremePoint(axis[0],axis[1],axis[2],out x,out y,out z);
    log.WriteLine("Extreme mm "+string.Join(",",axis)+": "+x*1000+", "+y*1000+", "+z*1000);
   }
   foreach(string ext in new[]{"SLDPRT","STEP","STL"}){
    int e=0,w=0;bool ok=doc.Extension.SaveAs(Path.Combine(output,"Bridge_TEST_300x150_R15_T10."+ext),0,1,null,ref e,ref w);
    log.WriteLine(ext+" save="+ok+" error="+e+" warning="+w);
    if(!ok||e!=0)throw new Exception("Export failed: "+ext+" "+e);
   }
   log.WriteLine("Preview BMP: "+doc.SaveBMP(Path.Combine(output,"preview.bmp"),1400,900));
  }
  Console.WriteLine(File.ReadAllText(Path.Combine(output,"verification.txt")));
 }
}
