using AMDtool.Properties;
using System.Windows;
using System.Windows.Controls;

#nullable disable
namespace AMDtool;

public partial class QuickInputPage : Page
{
  public string[] CustomLabels = new string[9];
  public string[] CustomTexts = new string[9];

  public QuickInputPage()
  {
    this.InitializeComponent();
    this.customList.SelectionChanged += new SelectionChangedEventHandler(this.CustomList_SelectionChanged);
    this.customList.SelectedIndex = 0;
    this.CustomLabels[0] = Settings.Default.clabel1;
    this.CustomLabels[1] = Settings.Default.clabel2;
    this.CustomLabels[2] = Settings.Default.clabel3;
    this.CustomLabels[3] = Settings.Default.clabel4;
    this.CustomLabels[4] = Settings.Default.clabel5;
    this.CustomLabels[5] = Settings.Default.clabel6;
    this.CustomLabels[6] = Settings.Default.clabel7;
    this.CustomLabels[7] = Settings.Default.clabel8;
    this.CustomLabels[8] = Settings.Default.clabel9;
    this.CustomTexts[0] = Settings.Default.custom1;
    this.CustomTexts[1] = Settings.Default.custom2;
    this.CustomTexts[2] = Settings.Default.custom3;
    this.CustomTexts[3] = Settings.Default.custom4;
    this.CustomTexts[4] = Settings.Default.custom5;
    this.CustomTexts[5] = Settings.Default.custom6;
    this.CustomTexts[6] = Settings.Default.custom7;
    this.CustomTexts[7] = Settings.Default.custom8;
    this.CustomTexts[8] = Settings.Default.custom9;
    this.LabelTb.TextChanged += new TextChangedEventHandler(this.LabelTb_TextChanged);
    this.SendTb.TextChanged += new TextChangedEventHandler(this.SendTb_TextChanged);
  }

  private void SendTb_TextChanged(object sender, TextChangedEventArgs e)
  {
    this.CustomTexts[this.customList.SelectedIndex] = this.SendTb.Text;
  }

  private void LabelTb_TextChanged(object sender, TextChangedEventArgs e)
  {
    this.CustomLabels[this.customList.SelectedIndex] = this.LabelTb.Text;
  }

  private void CustomList_SelectionChanged(object sender, SelectionChangedEventArgs e)
  {
    switch (this.customList.SelectedIndex)
    {
      case 0:
        this.LabelTb.Text = Settings.Default.clabel1;
        this.SendTb.Text = Settings.Default.custom1;
        break;
      case 1:
        this.LabelTb.Text = Settings.Default.clabel2;
        this.SendTb.Text = Settings.Default.custom2;
        break;
      case 2:
        this.LabelTb.Text = Settings.Default.clabel3;
        this.SendTb.Text = Settings.Default.custom3;
        break;
      case 3:
        this.LabelTb.Text = Settings.Default.clabel4;
        this.SendTb.Text = Settings.Default.custom4;
        break;
      case 4:
        this.LabelTb.Text = Settings.Default.clabel5;
        this.SendTb.Text = Settings.Default.custom5;
        break;
      case 5:
        this.LabelTb.Text = Settings.Default.clabel6;
        this.SendTb.Text = Settings.Default.custom6;
        break;
      case 6:
        this.LabelTb.Text = Settings.Default.clabel7;
        this.SendTb.Text = Settings.Default.custom7;
        break;
      case 7:
        this.LabelTb.Text = Settings.Default.clabel8;
        this.SendTb.Text = Settings.Default.custom8;
        break;
      case 8:
        this.LabelTb.Text = Settings.Default.clabel9;
        this.SendTb.Text = Settings.Default.custom9;
        break;
    }
  }
}
