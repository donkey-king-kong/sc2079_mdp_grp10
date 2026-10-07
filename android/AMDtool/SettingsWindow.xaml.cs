using AMDtool.Properties;
using System;
using System.Windows;
using System.Windows.Controls;

#nullable disable
namespace AMDtool;

public partial class SettingsWindow : Window
{
  private readonly ChangeCommandPage _changeCmdPg = new ChangeCommandPage();
  private readonly DelayOptionPage _delayOptionPage = new DelayOptionPage();
  private readonly QuickInputPage _quickInputPage = new QuickInputPage();
  private readonly RobotStatOptionPage _robotStatOptionPage = new RobotStatOptionPage();
  private readonly ScriptsPage _scriptsPage = new ScriptsPage();
  private readonly ToolsSettingPage _toolsSettingPage = new ToolsSettingPage();

  public SettingsWindow()
  {
    this.InitializeComponent();
    this.Navigator.SelectionChanged += new SelectionChangedEventHandler(this.Navigator_SelectionChanged);
    this.OkBtn.Click += new RoutedEventHandler(this.OkBtn_Click);
    this.CancelBtn.Click += new RoutedEventHandler(this.CancelBtn_Click);
    this.Navigator.SelectedIndex = 0;
  }

  private void CancelBtn_Click(object sender, RoutedEventArgs e) => this.Close();

  private void OkBtn_Click(object sender, RoutedEventArgs e)
  {
    Settings.Default.forward = this._changeCmdPg.Ftb.Text;
    Settings.Default.reverse = this._changeCmdPg.Rtb.Text;
    Settings.Default.turnLeft = this._changeCmdPg.RLtb.Text;
    Settings.Default.sendArena = this._changeCmdPg.SAtb.Text;
    Settings.Default.turnRight = this._changeCmdPg.RRtb.Text;
    Settings.Default.strafeLeft = this._changeCmdPg.SLtb.Text;
    Settings.Default.strafeRight = this._changeCmdPg.SRtb.Text;
    Settings.Default.startExplore = this._changeCmdPg.BEtb.Text;
    Settings.Default.startFastest = this._changeCmdPg.BFPtb.Text;
    Settings.Default.delayDuration = int.Parse(this._delayOptionPage.DurationTb.Text);
    Settings.Default.delay = this._delayOptionPage.StringTb.Text;
    Settings.Default.clabel1 = this._quickInputPage.CustomLabels[0];
    Settings.Default.clabel2 = this._quickInputPage.CustomLabels[1];
    Settings.Default.clabel3 = this._quickInputPage.CustomLabels[2];
    Settings.Default.clabel4 = this._quickInputPage.CustomLabels[3];
    Settings.Default.clabel5 = this._quickInputPage.CustomLabels[4];
    Settings.Default.clabel6 = this._quickInputPage.CustomLabels[5];
    Settings.Default.clabel7 = this._quickInputPage.CustomLabels[6];
    Settings.Default.clabel8 = this._quickInputPage.CustomLabels[7];
    Settings.Default.clabel9 = this._quickInputPage.CustomLabels[8];
    Settings.Default.custom1 = this._quickInputPage.CustomTexts[0];
    Settings.Default.custom2 = this._quickInputPage.CustomTexts[1];
    Settings.Default.custom3 = this._quickInputPage.CustomTexts[2];
    Settings.Default.custom4 = this._quickInputPage.CustomTexts[3];
    Settings.Default.custom5 = this._quickInputPage.CustomTexts[4];
    Settings.Default.custom6 = this._quickInputPage.CustomTexts[5];
    Settings.Default.custom7 = this._quickInputPage.CustomTexts[6];
    Settings.Default.custom8 = this._quickInputPage.CustomTexts[7];
    Settings.Default.custom9 = this._quickInputPage.CustomTexts[8];
    Settings.Default.msg1 = this._robotStatOptionPage.tb1.Text;
    Settings.Default.msg2 = this._robotStatOptionPage.tb2.Text;
    Settings.Default.msg3 = this._robotStatOptionPage.tb3.Text;
    Settings.Default.msg4 = this._robotStatOptionPage.tb4.Text;
    Settings.Default.msg5 = this._robotStatOptionPage.tb5.Text;
    Settings.Default.msg6 = this._robotStatOptionPage.tb6.Text;
    Settings.Default.msg7 = this._robotStatOptionPage.tb7.Text;
    Settings.Default.msg8 = this._robotStatOptionPage.tb8.Text;
    Settings.Default.msg9 = this._robotStatOptionPage.tb9.Text;
    Settings.Default.msg10 = this._robotStatOptionPage.tb10.Text;
    Settings.Default.msgEnable = this.FormMsgEnableNumber();
    Settings.Default.scriptFile = string.Concat(this._scriptsPage.FileList.SelectedItem);
    Settings.Default.arenaHeight = Convert.ToInt32(this._toolsSettingPage.HTb.Text);
    Settings.Default.arenaWidth = Convert.ToInt32(this._toolsSettingPage.WTb.Text);
    Settings.Default.robotSize = Convert.ToInt32(this._toolsSettingPage.sizeTb.Text);
    Settings.Default.robotAngle = Convert.ToInt32(this._toolsSettingPage.angleTb.Text);
    Settings.Default.Save();
    this.Close();
  }

  private void Navigator_SelectionChanged(object sender, SelectionChangedEventArgs e)
  {
    switch (this.Navigator.SelectedIndex)
    {
      case 0:
        this.SettingsFrame.Navigate((object) this._changeCmdPg);
        break;
      case 1:
        this.Navigator.SelectedIndex = 2;
        break;
      case 2:
        this.SettingsFrame.Navigate((object) this._delayOptionPage);
        break;
      case 3:
        this.SettingsFrame.Navigate((object) this._quickInputPage);
        break;
      case 4:
        this.SettingsFrame.Navigate((object) this._robotStatOptionPage);
        break;
      case 5:
        this.SettingsFrame.Navigate((object) this._scriptsPage);
        break;
      case 6:
        this.SettingsFrame.Navigate((object) this._toolsSettingPage);
        break;
    }
  }

  private long FormMsgEnableNumber()
  {
    long num = 20000000000;
    bool? isChecked1 = this._robotStatOptionPage.box1.IsChecked;
    bool flag1 = true;
    if ((isChecked1.GetValueOrDefault() == flag1 ? (isChecked1.HasValue ? 1 : 0) : 0) != 0)
      ++num;
    bool? isChecked2 = this._robotStatOptionPage.box2.IsChecked;
    bool flag2 = true;
    if ((isChecked2.GetValueOrDefault() == flag2 ? (isChecked2.HasValue ? 1 : 0) : 0) != 0)
      num += 10L;
    bool? isChecked3 = this._robotStatOptionPage.box3.IsChecked;
    bool flag3 = true;
    if ((isChecked3.GetValueOrDefault() == flag3 ? (isChecked3.HasValue ? 1 : 0) : 0) != 0)
      num += 100L;
    bool? isChecked4 = this._robotStatOptionPage.box4.IsChecked;
    bool flag4 = true;
    if ((isChecked4.GetValueOrDefault() == flag4 ? (isChecked4.HasValue ? 1 : 0) : 0) != 0)
      num += 1000L;
    bool? isChecked5 = this._robotStatOptionPage.box5.IsChecked;
    bool flag5 = true;
    if ((isChecked5.GetValueOrDefault() == flag5 ? (isChecked5.HasValue ? 1 : 0) : 0) != 0)
      num += 10000L;
    bool? isChecked6 = this._robotStatOptionPage.box6.IsChecked;
    bool flag6 = true;
    if ((isChecked6.GetValueOrDefault() == flag6 ? (isChecked6.HasValue ? 1 : 0) : 0) != 0)
      num += 100000L;
    bool? isChecked7 = this._robotStatOptionPage.box7.IsChecked;
    bool flag7 = true;
    if ((isChecked7.GetValueOrDefault() == flag7 ? (isChecked7.HasValue ? 1 : 0) : 0) != 0)
      num += 1000000L;
    bool? isChecked8 = this._robotStatOptionPage.box8.IsChecked;
    bool flag8 = true;
    if ((isChecked8.GetValueOrDefault() == flag8 ? (isChecked8.HasValue ? 1 : 0) : 0) != 0)
      num += 10000000L;
    bool? isChecked9 = this._robotStatOptionPage.box9.IsChecked;
    bool flag9 = true;
    if ((isChecked9.GetValueOrDefault() == flag9 ? (isChecked9.HasValue ? 1 : 0) : 0) != 0)
      num += 100000000L;
    bool? isChecked10 = this._robotStatOptionPage.box10.IsChecked;
    bool flag10 = true;
    if ((isChecked10.GetValueOrDefault() == flag10 ? (isChecked10.HasValue ? 1 : 0) : 0) != 0)
      num += 1000000000L;
    return num;
  }
}
