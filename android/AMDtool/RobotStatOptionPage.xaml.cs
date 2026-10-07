using AMDtool.Properties;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;

#nullable disable
namespace AMDtool;

public partial class RobotStatOptionPage : Page
{
  private bool[] _checkboxesChecked = new bool[10];

  public RobotStatOptionPage()
  {
    this.InitializeComponent();
    this.numTb.TextChanged += new TextChangedEventHandler(this.NumTb_TextChanged);
    long msgEnable = Settings.Default.msgEnable;
    for (int index = 0; index < 10; ++index)
    {
      this._checkboxesChecked[index] = msgEnable % 10L == 1L;
      msgEnable /= 10L;
    }
    this.box1.IsChecked = new bool?(this._checkboxesChecked[0]);
    this.box2.IsChecked = new bool?(this._checkboxesChecked[1]);
    this.box3.IsChecked = new bool?(this._checkboxesChecked[2]);
    this.box4.IsChecked = new bool?(this._checkboxesChecked[3]);
    this.box5.IsChecked = new bool?(this._checkboxesChecked[4]);
    this.box6.IsChecked = new bool?(this._checkboxesChecked[5]);
    this.box7.IsChecked = new bool?(this._checkboxesChecked[6]);
    this.box8.IsChecked = new bool?(this._checkboxesChecked[7]);
    this.box9.IsChecked = new bool?(this._checkboxesChecked[8]);
    this.box10.IsChecked = new bool?(this._checkboxesChecked[9]);
  }

  private void NumTb_TextChanged(object sender, TextChangedEventArgs e)
  {
    this.numTb.Text = new Regex("[^0-9]").Replace(this.numTb.Text, "");
    this.numTb.Select(this.numTb.Text.Length, 0);
  }
}
