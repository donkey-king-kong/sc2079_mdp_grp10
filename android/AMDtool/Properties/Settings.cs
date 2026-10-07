// Decompiled with JetBrains decompiler
// Type: AMDtool.Properties.Settings
// Assembly: AMDtool, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null
// MVID: 8D4CC376-ADB2-4178-ABF8-2A3517123F8A
// Assembly location: C:\Users\user\Downloads\AMDTOOL updated 08_Oct_2015\AMDtool.exe

using System.CodeDom.Compiler;
using System.ComponentModel;
using System.Configuration;
using System.Diagnostics;
using System.Runtime.CompilerServices;

#nullable disable
namespace AMDtool.Properties;

[CompilerGenerated]
[GeneratedCode("Microsoft.VisualStudio.Editors.SettingsDesigner.SettingsSingleFileGenerator", "14.0.0.0")]
public sealed class Settings : ApplicationSettingsBase
{
  private static Settings defaultInstance = (Settings) SettingsBase.Synchronized((SettingsBase) new Settings());

  private void SettingChangingEventHandler(object sender, SettingChangingEventArgs e)
  {
  }

  private void SettingsSavingEventHandler(object sender, CancelEventArgs e)
  {
  }

  public static Settings Default => Settings.defaultInstance;

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("f")]
  public string forward
  {
    get => (string) this[nameof (forward)];
    set => this[nameof (forward)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("r")]
  public string reverse
  {
    get => (string) this[nameof (reverse)];
    set => this[nameof (reverse)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("sl")]
  public string strafeLeft
  {
    get => (string) this[nameof (strafeLeft)];
    set => this[nameof (strafeLeft)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("sr")]
  public string strafeRight
  {
    get => (string) this[nameof (strafeRight)];
    set => this[nameof (strafeRight)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("tl")]
  public string turnLeft
  {
    get => (string) this[nameof (turnLeft)];
    set => this[nameof (turnLeft)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("tr")]
  public string turnRight
  {
    get => (string) this[nameof (turnRight)];
    set => this[nameof (turnRight)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("////----////")]
  public string delay
  {
    get => (string) this[nameof (delay)];
    set => this[nameof (delay)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("500")]
  public int delayDuration
  {
    get => (int) this[nameof (delayDuration)];
    set => this[nameof (delayDuration)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("q")]
  public string custom1
  {
    get => (string) this[nameof (custom1)];
    set => this[nameof (custom1)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("w")]
  public string custom2
  {
    get => (string) this[nameof (custom2)];
    set => this[nameof (custom2)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("e")]
  public string custom3
  {
    get => (string) this[nameof (custom3)];
    set => this[nameof (custom3)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("a")]
  public string custom4
  {
    get => (string) this[nameof (custom4)];
    set => this[nameof (custom4)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("s")]
  public string custom5
  {
    get => (string) this[nameof (custom5)];
    set => this[nameof (custom5)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("d")]
  public string custom6
  {
    get => (string) this[nameof (custom6)];
    set => this[nameof (custom6)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("z")]
  public string custom7
  {
    get => (string) this[nameof (custom7)];
    set => this[nameof (custom7)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("x")]
  public string custom8
  {
    get => (string) this[nameof (custom8)];
    set => this[nameof (custom8)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("c")]
  public string custom9
  {
    get => (string) this[nameof (custom9)];
    set => this[nameof (custom9)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("Custom 1")]
  public string clabel1
  {
    get => (string) this[nameof (clabel1)];
    set => this[nameof (clabel1)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("Custom 2")]
  public string clabel2
  {
    get => (string) this[nameof (clabel2)];
    set => this[nameof (clabel2)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("Custom 3")]
  public string clabel3
  {
    get => (string) this[nameof (clabel3)];
    set => this[nameof (clabel3)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("Custom 4")]
  public string clabel4
  {
    get => (string) this[nameof (clabel4)];
    set => this[nameof (clabel4)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("Custom 5")]
  public string clabel5
  {
    get => (string) this[nameof (clabel5)];
    set => this[nameof (clabel5)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("Custom 6")]
  public string clabel6
  {
    get => (string) this[nameof (clabel6)];
    set => this[nameof (clabel6)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("Custom 7")]
  public string clabel7
  {
    get => (string) this[nameof (clabel7)];
    set => this[nameof (clabel7)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("Custom 8")]
  public string clabel8
  {
    get => (string) this[nameof (clabel8)];
    set => this[nameof (clabel8)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("Custom 9")]
  public string clabel9
  {
    get => (string) this[nameof (clabel9)];
    set => this[nameof (clabel9)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("{\"status\":\"exploring\"}")]
  public string msg1
  {
    get => (string) this[nameof (msg1)];
    set => this[nameof (msg1)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("{\"status\":\"fastest path\"}")]
  public string msg2
  {
    get => (string) this[nameof (msg2)];
    set => this[nameof (msg2)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("{\"status\":\"turning left\"}")]
  public string msg3
  {
    get => (string) this[nameof (msg3)];
    set => this[nameof (msg3)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("{\"status\":\"turning right\"}")]
  public string msg4
  {
    get => (string) this[nameof (msg4)];
    set => this[nameof (msg4)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("{\"status\":\"moving forward\"}")]
  public string msg5
  {
    get => (string) this[nameof (msg5)];
    set => this[nameof (msg5)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("{\"status\":\"reversing\"}")]
  public string msg6
  {
    get => (string) this[nameof (msg6)];
    set => this[nameof (msg6)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("{\"status\":\"taking over the world\"}")]
  public string msg7
  {
    get => (string) this[nameof (msg7)];
    set => this[nameof (msg7)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("{\"status\":\"accessing nuclear codes\"}")]
  public string msg8
  {
    get => (string) this[nameof (msg8)];
    set => this[nameof (msg8)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("{\"status\":\"reading your mind\"}")]
  public string msg9
  {
    get => (string) this[nameof (msg9)];
    set => this[nameof (msg9)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("{\"status\":\"10010101\"}")]
  public string msg10
  {
    get => (string) this[nameof (msg10)];
    set => this[nameof (msg10)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("20000111111")]
  public long msgEnable
  {
    get => (long) this[nameof (msgEnable)];
    set => this[nameof (msgEnable)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("10")]
  public int msgNum
  {
    get => (int) this[nameof (msgNum)];
    set => this[nameof (msgNum)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("defaultJson.cs")]
  public string scriptFile
  {
    get => (string) this[nameof (scriptFile)];
    set => this[nameof (scriptFile)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("15")]
  public int arenaWidth
  {
    get => (int) this[nameof (arenaWidth)];
    set => this[nameof (arenaWidth)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("20")]
  public int arenaHeight
  {
    get => (int) this[nameof (arenaHeight)];
    set => this[nameof (arenaHeight)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("3")]
  public int robotSize
  {
    get => (int) this[nameof (robotSize)];
    set => this[nameof (robotSize)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("0")]
  public int robotAngle
  {
    get => (int) this[nameof (robotAngle)];
    set => this[nameof (robotAngle)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("beginExplore")]
  public string startExplore
  {
    get => (string) this[nameof (startExplore)];
    set => this[nameof (startExplore)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("beginFastest")]
  public string startFastest
  {
    get => (string) this[nameof (startFastest)];
    set => this[nameof (startFastest)] = (object) value;
  }

  [UserScopedSetting]
  [DebuggerNonUserCode]
  [DefaultSettingValue("sendArena")]
  public string sendArena
  {
    get => (string) this[nameof (sendArena)];
    set => this[nameof (sendArena)] = (object) value;
  }
}
