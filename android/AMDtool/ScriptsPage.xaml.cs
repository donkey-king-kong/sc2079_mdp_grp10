using AMDtool.Properties;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;

#nullable disable
namespace AMDtool;

public partial class ScriptsPage : Page
{
  public ScriptsPage()
  {
    this.InitializeComponent();
    try
    {
      string path = Path.GetDirectoryName(Assembly.GetEntryAssembly().Location) + "\\scripts\\";
      DirectoryInfo directoryInfo = new DirectoryInfo(path);
      int num = 0;
      foreach (FileInfo file in directoryInfo.GetFiles())
      {
        this.FileList.Items.Add((object) file);
        if (file.FullName.Equals(path + Settings.Default.scriptFile))
          this.FileList.SelectedIndex = num;
        ++num;
      }
    }
    catch (DirectoryNotFoundException ex)
    {
    }
  }
}
