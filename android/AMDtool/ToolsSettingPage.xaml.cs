using AMDtool.Properties;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;

#nullable disable
namespace AMDtool;

public partial class ToolsSettingPage : Page
{
  public ToolsSettingPage()
  {
    this.InitializeComponent();
    this.sizeTb.Text = string.Concat((object) Settings.Default.robotSize);
    this.WTb.Text = string.Concat((object) Settings.Default.arenaWidth);
    this.HTb.Text = string.Concat((object) Settings.Default.arenaHeight);
    this.angleTb.Text = string.Concat((object) Settings.Default.robotAngle);
    this.sizeTb.TextChanged += new TextChangedEventHandler(this.TextBChanged);
    this.WTb.TextChanged += new TextChangedEventHandler(this.TextBChanged);
    this.HTb.TextChanged += new TextChangedEventHandler(this.TextBChanged);
    this.angleTb.TextChanged += new TextChangedEventHandler(this.TextBChanged);
  }

  private void TextBChanged(object sender, TextChangedEventArgs e)
  {
    Regex regex = new Regex("[^0-9]");
    TextBox textBox1 = (TextBox) sender;
    textBox1.Text = regex.Replace(textBox1.Text, "");
    TextBox textBox2 = textBox1;
    textBox2.Select(textBox2.Text.Length, 0);
  }
}
