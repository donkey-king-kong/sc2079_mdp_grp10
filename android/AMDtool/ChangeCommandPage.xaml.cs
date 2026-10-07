using AMDtool.Properties;
using System.Windows;
using System.Windows.Controls;

#nullable disable
namespace AMDtool;

public partial class ChangeCommandPage : Page
{
  public ChangeCommandPage()
  {
    this.InitializeComponent();
    this.SLtb.Text = Settings.Default.strafeLeft;
    this.SRtb.Text = Settings.Default.strafeRight;
    this.RLtb.Text = Settings.Default.turnLeft;
    this.RRtb.Text = Settings.Default.turnRight;
    this.Ftb.Text = Settings.Default.forward;
    this.Rtb.Text = Settings.Default.reverse;
    this.BEtb.Text = Settings.Default.startExplore;
    this.BFPtb.Text = Settings.Default.startFastest;
    this.SAtb.Text = Settings.Default.sendArena;
  }
}
