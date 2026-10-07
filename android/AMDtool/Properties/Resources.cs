// Decompiled with JetBrains decompiler
// Type: AMDtool.Properties.Resources
// Assembly: AMDtool, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null
// MVID: 8D4CC376-ADB2-4178-ABF8-2A3517123F8A
// Assembly location: C:\Users\user\Downloads\AMDTOOL updated 08_Oct_2015\AMDtool.exe

using System.CodeDom.Compiler;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Resources;
using System.Runtime.CompilerServices;

#nullable disable
namespace AMDtool.Properties;

[GeneratedCode("System.Resources.Tools.StronglyTypedResourceBuilder", "4.0.0.0")]
[DebuggerNonUserCode]
[CompilerGenerated]
internal class Resources
{
  private static ResourceManager resourceMan;
  private static CultureInfo resourceCulture;

  internal Resources()
  {
  }

  [EditorBrowsable(EditorBrowsableState.Advanced)]
  internal static ResourceManager ResourceManager
  {
    get
    {
      if (AMDtool.Properties.Resources.resourceMan == null)
        AMDtool.Properties.Resources.resourceMan = new ResourceManager("AMDtool.Properties.Resources", typeof (AMDtool.Properties.Resources).Assembly);
      return AMDtool.Properties.Resources.resourceMan;
    }
  }

  [EditorBrowsable(EditorBrowsableState.Advanced)]
  internal static CultureInfo Culture
  {
    get => AMDtool.Properties.Resources.resourceCulture;
    set => AMDtool.Properties.Resources.resourceCulture = value;
  }
}
