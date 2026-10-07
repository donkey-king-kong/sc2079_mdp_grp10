using AMDtool.Properties;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;

#nullable disable
namespace AMDtool;

public partial class DelayOptionPage : Page
{
  public DelayOptionPage()
  {
    this.InitializeComponent();
    this.DurationTb.Text = string.Concat((object) Settings.Default.delayDuration);
    this.StringTb.Text = Settings.Default.delay;
    this.DurationTb.TextChanged += new TextChangedEventHandler(this.DurationTb_TextChanged);
  }

  private void DurationTb_TextChanged(object sender, TextChangedEventArgs e)
  {
    this.DurationTb.Text = new Regex("[^0-9]").Replace(this.DurationTb.Text, "");
    this.DurationTb.Select(this.DurationTb.Text.Length, 0);
  }
}
