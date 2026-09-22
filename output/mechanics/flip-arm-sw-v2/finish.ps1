$ErrorActionPreference='Stop'
$api='C:\Program Files\SOLIDWORKS Corp\SOLIDWORKS\api\redist\SolidWorks.Interop.sldworks.dll'
Add-Type -Path $api
Add-Type -ReferencedAssemblies $api -TypeDefinition @'
using System;using System.IO;using SolidWorks.Interop.sldworks;
public class FinishArmV2 {
 static void Save(IModelDoc2 d,string p){int e=0,w=0;if(!d.Extension.SaveAs(p,0,1,null,ref e,ref w)||e!=0)throw new Exception("Save "+p+" "+e);}
 public static void Run(string dir){
 var sw=(ISldWorks)Activator.CreateInstance(Type.GetTypeFromProgID("SldWorks.Application"));
 var d=(IModelDoc2)sw.GetOpenDocumentByName(Path.Combine(dir,"V2_bin_180x143x80.SLDPRT"));
 if(d==null)throw new Exception("Bin not open");
 var p=(IFeature)d.FirstFeature();while(p!=null&&p.GetTypeName2()!="RefPlane")p=(IFeature)p.GetNextFeature();
 d.ClearSelection2(true);p.Select2(false,0);d.SketchManager.InsertSketch(true);
 d.SketchManager.CreateCornerRectangle(-.001,.0195,0,.010,.0275,0);d.SketchManager.InsertSketch(true);
 var f=d.FeatureManager.FeatureCut3(true,false,true,0,0,.058,0,false,false,false,false,0,0,false,false,false,false,false,true,true,true,true,false,0,0,false);
 if(f==null)throw new Exception("Relief failed");f.Name="Servo_stand_relief_W8_H58";d.EditRebuild3();bool warning;int err=f.GetErrorCode2(out warning);if(err!=0)throw new Exception("Relief rebuild error "+err);
 object[] bodies=(object[])((IPartDoc)d).GetBodies2(0,false);if(bodies.Length!=1)throw new Exception("Bin body count");
 Save(d,Path.Combine(dir,"V2_bin_180x143x80.SLDPRT"));Save(d,Path.Combine(dir,"V2_bin_180x143x80.STEP"));
 foreach(string pose in new[]{"pickup","raised","deposit"}){
 d=(IModelDoc2)sw.GetOpenDocumentByName(Path.Combine(dir,"FlipArm_V2_"+pose+".SLDASM"));if(d==null)throw new Exception("Pose missing");
 int ae=0;sw.ActivateDoc3(d.GetTitle(),false,0,ref ae);d.ForceRebuild3(false);d.ClearSelection2(true);d.ShowNamedView2("",7);d.ViewZoomtofit2();
 Save(d,Path.Combine(dir,"FlipArm_V2_"+pose+".SLDASM"));Save(d,Path.Combine(dir,"FlipArm_V2_"+pose+".STEP"));d.SaveBMP(Path.Combine(dir,pose+".bmp"),1500,1050);
 }
 File.WriteAllText(Path.Combine(dir,"finish-check.txt"),"Bin relief: width8 x depth10 x height58 mm. One solid. Feature error0. Three poses rebuilt and re-exported.\n");
 }
}
'@
[FinishArmV2]::Run($PSScriptRoot)
