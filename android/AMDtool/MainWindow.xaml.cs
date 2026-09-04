using AMDtool.Properties;
using InTheHand.Net;
using InTheHand.Net.Bluetooth;
using InTheHand.Net.Sockets;
using Microsoft.CSharp;
using Microsoft.Win32;
using System;
using System.CodeDom.Compiler;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;
using System.Web.Script.Serialization;

#nullable disable
namespace AMDtool;

public class TabletUpdate
{
    public string Type { get; set; }
    public int X { get; set; }
    public int Y { get; set; }
    public int Direction { get; set; }
}

public partial class MainWindow : Window
{
  private bool _sendGrid;
  private bool _addObstacleEnabled;
  private bool _addRobotEnabled;
  private int[,] _localGridLayout;
  private int[] _robotPosition = new int[3];
  private Grid _robot;
  private const int XOffset = 0;
  private const int YOffset = 0;
  private BluetoothAddress _btAddress;
  private BluetoothDeviceInfo _selectedDevice;
  private BluetoothClient _btClient;
  private NetworkStream _stream;
  private byte[] _readBuffer = new byte[1024];
  private BluetoothData _btData = new BluetoothData();
  public StringBuilder CommandLog = new StringBuilder();
  public StringBuilder ReceivedLog = new StringBuilder();
  private ExpandedCmdWindow _ecw = new ExpandedCmdWindow();
  private ExpandedReceivedWindow _erw = new ExpandedReceivedWindow();

  public MainWindow()
  {
    this.InitializeComponent();
    this.Style = (Style) this.FindResource((object) typeof (Window));
    Application.Current.ShutdownMode = ShutdownMode.OnLastWindowClose;
    this.GenerateNewArena(Settings.Default.arenaWidth, Settings.Default.arenaHeight);
    this.StartBluetoothListen();
    try
    {
      this._btClient = new BluetoothClient();
    }
    catch (PlatformNotSupportedException ex)
    {
    }
    this.Closed += new EventHandler(this.MainWindow_Closed);
    this.SendExpander.Collapsed += new RoutedEventHandler(this.SendExpander_Collapsed);
    this.SendExpander.Expanded += new RoutedEventHandler(this.SendExpander_Expanded);
    this.ReceivedExpander.Collapsed += new RoutedEventHandler(this.ReceivedExpander_Collapsed);
    this.ReceivedExpander.Expanded += new RoutedEventHandler(this.ReceivedExpander_Expanded);
    this.CommandExpander.Collapsed += new RoutedEventHandler(this.CommandExpander_Collapsed);
    this.CommandExpander.Expanded += new RoutedEventHandler(this.CommandExpander_Expanded);
    this.SendBtn.Click += new RoutedEventHandler(this.SendBtn_Click);
    this.ClearBtn.Click += new RoutedEventHandler(this.ClearBtn_Click);
    this.GenerateArenaMenu.IsKeyboardFocusWithinChanged += new DependencyPropertyChangedEventHandler(this.GenerateArenaMenu_IsKeyboardFocusWithinChanged);
    this.RobotSizeDropDownListBox.SelectionChanged += new SelectionChangedEventHandler(this.RobotSizeDropDownListBox_SelectionChanged);
    this.RobotSizeDropdownGrid.IsKeyboardFocusWithinChanged += new DependencyPropertyChangedEventHandler(this.RobotSizeDropdownGrid_IsKeyboardFocusWithinChanged);
    this.ArenaButton.Click += new RoutedEventHandler(this.ArenaButton_Click);
    this.GenerateArenaButton.Click += new RoutedEventHandler(this.GenerateArenaButton_Click);
    this.AddObstacleButton.Click += new RoutedEventHandler(this.AddObstacleButton_Click);
    this.RemoveObstacleButton.Click += new RoutedEventHandler(this.RemoveObstacleButton_Click);
    this.AddRobotButton.Click += new RoutedEventHandler(this.AddRobotButton_Click);
    this.RobotSizeButton.Click += new RoutedEventHandler(this.RobotSizeButton_Click);
    this.RotateLeftButton.Click += new RoutedEventHandler(this.RotateLeftButton_Click);
    this.RotateRightButton.Click += new RoutedEventHandler(this.RotateRightButton_Click);
    this.DirectionTextBox.TextChanged += new TextChangedEventHandler(this.DirectionTextBox_TextChanged);
    this.ScanBtItem.Click += new RoutedEventHandler(this.BluetoothItem_Click);
    this.DisconnectItem.Click += new RoutedEventHandler(this.DisconnectItem_Click);
    this.ReconnectBtItem.Click += new RoutedEventHandler(this.ReconnectBtItem_Click);
    this.RemovePairedItem.Click += new RoutedEventHandler(this.RemovePairedItem_Click);
    this.SettingsItem.Click += new RoutedEventHandler(this.SettingsItem_Click);
    this.AboutItem.Click += new RoutedEventHandler(this.AboutItem_Click);
    this.SendMoreBtn.Click += new RoutedEventHandler(this.SendMoreBtn_Click);
    this.SendGridBtn.Click += new RoutedEventHandler(this.SendGridBtn_Click);
    this.SaveReceivedBtn.Click += new RoutedEventHandler(this.SaveReceivedBtn_Click);
    this.ClearReceivedBtn.Click += new RoutedEventHandler(this.ClearReceivedBtn_Click);
    this.CopyReceivedBtn.Click += new RoutedEventHandler(this.CopyReceivedBtn_Click);
    this.ExpandReceivedBtn.Click += new RoutedEventHandler(this.ExpandReceivedBtn_Click);
    this.SaveCmdBtn.Click += new RoutedEventHandler(this.SaveCmdBtn_Click);
    this.ClearCmdBtn.Click += new RoutedEventHandler(this.ClearCmdBtn_Click);
    this.CopyCmdBtn.Click += new RoutedEventHandler(this.CopyCmdBtn_Click);
    this.ExpandCmdBtn.Click += new RoutedEventHandler(this.ExpandCmdBtn_Click);
    this._erw.ClearReceivedBtn.Click += new RoutedEventHandler(this.ClearReceivedBtn_Click);
    this._erw.CopyReceivedBtn.Click += new RoutedEventHandler(this.CopyReceivedBtn_Click);
    this._erw.SaveReceivedBtn.Click += new RoutedEventHandler(this.SaveReceivedBtn_Click);
    this._erw.Closing += new System.ComponentModel.CancelEventHandler(this._erw_Closing);
    this._ecw.SaveCmdBtn.Click += new RoutedEventHandler(this.SaveCmdBtn_Click);
    this._ecw.ClearCmdBtn.Click += new RoutedEventHandler(this.ClearCmdBtn_Click);
    this._ecw.CopyCmdBtn.Click += new RoutedEventHandler(this.CopyCmdBtn_Click);
    this._ecw.Closing += new System.ComponentModel.CancelEventHandler(this._ecw_Closing);
    this.SaveItem.Click += new RoutedEventHandler(this.SaveItem_Click);
    this.LoadFileItem.Click += new RoutedEventHandler(this.LoadFileItem_Click);
    this.SetInitialValues();
  }

  private void AboutItem_Click(object sender, RoutedEventArgs e) => new About().Show();

  private void ReconnectBtItem_Click(object sender, RoutedEventArgs e)
  {
    try
    {
      this.Disconnect();
      this._btClient.Connect(new BluetoothEndPoint(this._btAddress, BluetoothService.SerialPort));
      this.BtLabel1.Opacity = 0.0;
      this.BtLabel2.Opacity = 1.0;
      this.BtLabel2.Content = (object) ("CONNECTED TO " + this._btClient.RemoteMachineName);
      this._stream = (NetworkStream) this._btClient.GetStream();
      this._btData.Stream = this._stream;
      this._btData.BtClient = this._btClient;
      this._stream.BeginRead(this._readBuffer, 0, this._readBuffer.Length, new AsyncCallback(this.BluetoothReceiverCallback), (object) this._btData);
      this.ShowToast("Connected to " + this._btClient.RemoteMachineName);
    }
    catch (SocketException ex)
    {
      this.ShowToast("You have not previously connected to a device.");
      this.BluetoothItem_Click((object) null, (RoutedEventArgs) null);
    }
  }

  private void LoadFileItem_Click(object sender, RoutedEventArgs e)
  {
    OpenFileDialog openFileDialog = new OpenFileDialog();
    bool? nullable = openFileDialog.ShowDialog();
    bool flag = true;
    if ((nullable.GetValueOrDefault() == flag ? (nullable.HasValue ? 1 : 0) : 0) == 0)
      return;
    int int32_1;
    int int32_2;
    int int32_3;
    int num;
    int int32_4;
    int int32_5;
    string hexLayout;
    using (StreamReader streamReader = new StreamReader(openFileDialog.FileName))
    {
      int32_1 = Convert.ToInt32(streamReader.ReadLine());
      int32_2 = Convert.ToInt32(streamReader.ReadLine());
      int32_3 = Convert.ToInt32(streamReader.ReadLine());
      num = Convert.ToInt32(streamReader.ReadLine()) + 360;
      int32_4 = Convert.ToInt32(streamReader.ReadLine());
      int32_5 = Convert.ToInt32(streamReader.ReadLine());
      hexLayout = streamReader.ReadLine();
    }
    this._robotPosition[0] = int32_2;
    this._robotPosition[1] = int32_3;
    this.GenerateNewArena(int32_4, int32_5, hexLayout, new int[4]
    {
      int32_2,
      int32_3,
      num,
      int32_1
    });
  }

  private void SaveItem_Click(object sender, RoutedEventArgs e)
  {
    SaveFileDialog saveFileDialog1 = new SaveFileDialog();
    saveFileDialog1.Filter = "Text files (*.txt)|*.txt";
    SaveFileDialog saveFileDialog2 = saveFileDialog1;
    bool? nullable = saveFileDialog2.ShowDialog();
    bool flag = true;
    if ((nullable.GetValueOrDefault() == flag ? (nullable.HasValue ? 1 : 0) : 0) == 0)
      return;
    int num1 = this.RobotSizeDropDownListBox.SelectedIndex + 1;
    int length1 = this._localGridLayout.GetLength(0);
    int length2 = this._localGridLayout.GetLength(1);
    int num2 = 0;
    string str1 = "";
    string str2 = "";
    for (int index1 = 0; index1 < length2; ++index1)
    {
      for (int index2 = 0; index2 < length1; ++index2)
      {
        ++num2;
        str2 += (string) (object) this._localGridLayout[index2, index1];
        if (num2 % 4 == 0)
        {
          str1 += Convert.ToInt64(str2, 2).ToString("x");
          str2 = "";
          num2 = 0;
        }
      }
    }
    if (!str2.Equals(""))
    {
      for (int index = num2; index < 4; ++index)
        str2 += (string) (object) 0;
      str1 += Convert.ToInt64(str2, 2).ToString("x");
    }
    using (StreamWriter streamWriter = new StreamWriter(saveFileDialog2.FileName))
    {
      streamWriter.WriteLine(num1);
      streamWriter.WriteLine(this._robotPosition[0]);
      streamWriter.WriteLine(this._robotPosition[1]);
      streamWriter.WriteLine(this._robotPosition[2]);
      streamWriter.WriteLine(length1);
      streamWriter.WriteLine(length2);
      streamWriter.WriteLine(str1);
    }
  }

  private void SettingsItem_Click(object sender, RoutedEventArgs e) => new SettingsWindow().Show();

  private void SendMoreBtn_Click(object sender, RoutedEventArgs e)
  {
    if (this._btClient != null)
    {
      if (this._btClient.Connected)
        new SendMoreWindow() { BtClient = this._btClient }.Show();
      else
        this.ShowToast("Please connect to a bluetooth device first. ;)");
    }
    else
      this.ShowToast("Please connect to a bluetooth device first. ;)");
  }

  private void SendGridBtn_Click(object sender, RoutedEventArgs e)
  {
    this._sendGrid = !this._sendGrid;
    if (this._sendGrid)
      this.SendGridBtn.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 50, (byte) 0, (byte) 0, (byte) 0));
    else
      this.SendGridBtn.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 0, (byte) 0, (byte) 0, (byte) 0));
  }

  private void RemovePairedItem_Click(object sender, RoutedEventArgs e)
  {
    new RemovedPairedWindow().Show();
  }

  private void SaveCmdBtn_Click(object sender, RoutedEventArgs e)
  {
    SaveFileDialog saveFileDialog1 = new SaveFileDialog();
    saveFileDialog1.Filter = "Text files (*.txt)|*.txt|All files (*.*)|*.*";
    SaveFileDialog saveFileDialog2 = saveFileDialog1;
    bool? nullable = saveFileDialog2.ShowDialog();
    bool flag = true;
    if ((nullable.GetValueOrDefault() == flag ? (nullable.HasValue ? 1 : 0) : 0) == 0)
      return;
    string[] strArray = new Regex("\n").Split(this.CommandLogTextBlock.Text);
    using (StreamWriter streamWriter = new StreamWriter(saveFileDialog2.FileName))
    {
      foreach (string str in strArray)
        streamWriter.WriteLine(str);
    }
  }

  private void SaveReceivedBtn_Click(object sender, RoutedEventArgs e)
  {
    SaveFileDialog saveFileDialog1 = new SaveFileDialog();
    saveFileDialog1.Filter = "Text files (*.txt)|*.txt|All files (*.*)|*.*";
    SaveFileDialog saveFileDialog2 = saveFileDialog1;
    bool? nullable = saveFileDialog2.ShowDialog();
    bool flag = true;
    if ((nullable.GetValueOrDefault() == flag ? (nullable.HasValue ? 1 : 0) : 0) == 0)
      return;
    string[] strArray = new Regex("\n").Split(this.ReceivedTextBlock.Text);
    using (StreamWriter streamWriter = new StreamWriter(saveFileDialog2.FileName))
    {
      foreach (string str in strArray)
        streamWriter.WriteLine(str);
    }
  }

  private void _erw_Closing(object sender, System.ComponentModel.CancelEventArgs e)
  {
    e.Cancel = true;
    this._erw.Hide();
  }

  private void _ecw_Closing(object sender, System.ComponentModel.CancelEventArgs e)
  {
    e.Cancel = true;
    this._ecw.Hide();
  }

  private void MainWindow_Closed(object sender, EventArgs e) => Application.Current.Shutdown();

  private void ExpandCmdBtn_Click(object sender, RoutedEventArgs e)
  {
    this._ecw.CmdTextBlock.Text = this.CommandLogTextBlock.Text;
    this._ecw.Show();
  }

  private void ExpandReceivedBtn_Click(object sender, RoutedEventArgs e)
  {
    this._erw.ReceivedTextBlock.Text = this.ReceivedTextBlock.Text;
    this._erw.Show();
  }

  private void CopyCmdBtn_Click(object sender, RoutedEventArgs e)
  {
    Clipboard.SetText(this.CommandLogTextBlock.Text);
  }

  private void CopyReceivedBtn_Click(object sender, RoutedEventArgs e)
  {
    Clipboard.SetText(this.ReceivedTextBlock.Text);
  }

  private void ClearCmdBtn_Click(object sender, RoutedEventArgs e)
  {
    this.CommandLogTextBlock.Text = "";
    this._ecw.CmdTextBlock.Text = "";
    this.CommandLog.Clear();
  }

  private void ClearReceivedBtn_Click(object sender, RoutedEventArgs e)
  {
    this.ReceivedTextBlock.Text = "";
    this._erw.ReceivedTextBlock.Text = "";
    this.ReceivedLog.Clear();
  }

  private void GenerateArenaMenu_IsKeyboardFocusWithinChanged(
    object sender,
    DependencyPropertyChangedEventArgs e)
  {
    if (this.GenerateArenaMenu.IsKeyboardFocusWithin)
      return;
    this.GenerateArenaMenu.Opacity = 0.0;
    this.GenerateArenaMenu.IsEnabled = false;
    this.GenerateArenaMenu.IsHitTestVisible = false;
  }

  private void Disconnect()
  {
    this._btClient.Close();
    this._btClient = new BluetoothClient();
    this.BtLabel1.Opacity = 1.0;
    this.BtLabel2.Opacity = 0.0;
    this.StartBluetoothListen();
  }

  private void DisconnectItem_Click(object sender, RoutedEventArgs e)
  {
    if (!this._btClient.Connected)
      return;
    this.Disconnect();
  }

  private void ArenaButton_Click(object sender, RoutedEventArgs e)
  {
    this.GenerateArenaMenu.IsHitTestVisible = true;
    this.GenerateArenaMenu.Opacity = 1.0;
    this.GenerateArenaMenu.IsEnabled = true;
    this.XTextBox.Focus();
  }

  private void ClearBtn_Click(object sender, RoutedEventArgs e) => this.SendTextBox.Text = "";

  private void SendBtn_Click(object sender, RoutedEventArgs e)
  {
    try
    {
      NetworkStream stream = this._btClient.GetStream();
      byte[] bytes = Encoding.Default.GetBytes(this.SendTextBox.Text);
      byte[] buffer = bytes;
      int length = bytes.Length;
      ((Stream) stream).Write(buffer, 0, length);
    }
    catch (InvalidOperationException ex)
    {
      this.ShowToast("Opps! You are not currently connected to a bluetooth device.");
    }
    catch (NullReferenceException ex)
    {
      Console.WriteLine("Null reference");
    }
    catch (IOException ex)
    {
      this.Disconnect();
      this.ShowToast("Your connected device had disconnected. Connection was lost.");
    }
  }

  private void BluetoothItem_Click(object sender, RoutedEventArgs e)
  {
    BluetoothWindow bluetoothWindow = new BluetoothWindow();
    bluetoothWindow.ShowDialog();
    if (bluetoothWindow.Cancel)
      return;
    try
    {
      this._selectedDevice = bluetoothWindow.SelectedDevice;
      this._btAddress = this._selectedDevice.DeviceAddress;
      BluetoothEndPoint remoteEP = new BluetoothEndPoint(this._btAddress, BluetoothService.SerialPort);
      if (this._btClient.Connected)
        this.Disconnect();
      BluetoothSecurity.PairRequest(this._btAddress, "pin");
      try
      {
        this._btClient.Connect(remoteEP);
      }
      catch (InvalidOperationException ex)
      {
        this.Disconnect();
        this._btClient.Connect(remoteEP);
      }
      this.BtLabel1.Opacity = 0.0;
      this.BtLabel2.Opacity = 1.0;
      this.BtLabel2.Content = (object) ("CONNECTED TO " + this._selectedDevice.DeviceName);
      this._stream = (NetworkStream) this._btClient.GetStream();
      this._btData.Stream = this._stream;
      this._btData.BtClient = this._btClient;
      this._stream.BeginRead(this._readBuffer, 0, this._readBuffer.Length, new AsyncCallback(this.BluetoothReceiverCallback), (object) this._btData);
      this.ShowToast("Connected to " + this._selectedDevice.DeviceName);
    }
    catch (NullReferenceException ex)
    {
      Console.WriteLine("Connection fail. Null reference exception.");
    }
    catch (SocketException ex)
    {
      Console.WriteLine("Error code: " + (object) ex.ErrorCode);
      if (ex.ErrorCode == 10049)
        this.ShowToast("Opps! The required software may not be opened on the connected device, OR you have already connected. ");
      else
        Console.WriteLine("Some socket exception");
    }
    catch (PlatformNotSupportedException ex)
    {
      this.ShowToast("Bluetooth is not supported on this device. :(");
    }
  }

  private void RobotSizeDropDownListBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
  {
    int num = this.RobotSizeDropDownListBox.SelectedIndex + 1;
    double angle = double.Parse(this.DirectionTextBox.Text);
    this._robot.RenderTransform = (Transform) new TransformGroup()
    {
      Children = {
        (Transform) new RotateTransform(angle, this._robot.Width / 2.0, this._robot.Height / 2.0),
        (Transform) new ScaleTransform((double) num, (double) num)
      }
    };
  }

  private void RobotSizeButton_Click(object sender, RoutedEventArgs e)
  {
    this.RobotSizeDropdownGrid.Opacity = 1.0;
    this.RobotSizeDropdownGrid.IsHitTestVisible = true;
    this.RobotSizeDropdownGrid.IsEnabled = true;
    this.RobotSizeDropDownListBox.Focus();
  }

  private void RobotSizeDropdownGrid_IsKeyboardFocusWithinChanged(
    object sender,
    DependencyPropertyChangedEventArgs e)
  {
    if (this.RobotSizeDropdownGrid.IsKeyboardFocusWithin)
      return;
    this.RobotSizeDropdownGrid.Opacity = 0.0;
    this.RobotSizeDropdownGrid.IsHitTestVisible = false;
    this.RobotSizeDropdownGrid.IsEnabled = false;
  }

  private void RotateRightButton_Click(object sender, RoutedEventArgs e)
  {
    double num = double.Parse(this.DirectionTextBox.Text);
    if (num >= 0.0 && num < 90.0)
      this.DirectionTextBox.Text = "90";
    else if (num >= 90.0 && num < 180.0)
      this.DirectionTextBox.Text = "180";
    else if (num >= 180.0 && num < 270.0)
    {
      this.DirectionTextBox.Text = "270";
    }
    else
    {
      if (num < 270.0 || num >= 360.0)
        return;
      this.DirectionTextBox.Text = "0";
    }
  }

  private void RotateLeftButton_Click(object sender, RoutedEventArgs e)
  {
    double num = double.Parse(this.DirectionTextBox.Text);
    if (num > 0.0 && num <= 90.0)
      this.DirectionTextBox.Text = "0";
    else if (num > 90.0 && num <= 180.0)
      this.DirectionTextBox.Text = "90";
    else if (num > 180.0 && num <= 270.0)
    {
      this.DirectionTextBox.Text = "180";
    }
    else
    {
      if ((num <= 270.0 || num >= 360.0) && num != 0.0)
        return;
      this.DirectionTextBox.Text = "270";
    }
  }

  private void DirectionTextBox_TextChanged(object sender, TextChangedEventArgs e)
  {
    try
    {
      double angle = double.Parse(this.DirectionTextBox.Text);
      while (angle >= 360.0)
        angle -= 360.0;
      if (angle < 0.0)
        angle = 0.0;
      this.DirectionTextBox.Text = string.Concat((object) angle);
      int num = this.RobotSizeDropDownListBox.SelectedIndex + 1;
      this._robot.RenderTransform = (Transform) new TransformGroup()
      {
        Children = {
          (Transform) new RotateTransform(angle, this._robot.Width / 2.0, this._robot.Height / 2.0),
          (Transform) new ScaleTransform((double) num, (double) num)
        }
      };
      this._robotPosition[2] = (int) angle;
      if (!this._sendGrid)
        return;
      this.RunSendGridFormattingScript(new bool?(true), new bool?(false), (int[]) null);
    }
    catch (FormatException ex)
    {
    }
  }

  private void AddRobotButton_Click(object sender, RoutedEventArgs e)
  {
    this._addObstacleEnabled = false;
    if (this._addRobotEnabled)
    {
      this._addRobotEnabled = false;
      this.AddRobotButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 0, (byte) 0, (byte) 0, (byte) 0));
    }
    else
    {
      this._addRobotEnabled = true;
      this.AddRobotButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 44, (byte) 0, (byte) 0, (byte) 0));
      this.RemoveObstacleButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 0, (byte) 0, (byte) 0, (byte) 0));
      this.AddObstacleButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 0, (byte) 0, (byte) 0, (byte) 0));
    }
  }

  private void RemoveObstacleButton_Click(object sender, RoutedEventArgs e)
  {
    this._addObstacleEnabled = false;
    this._addRobotEnabled = false;
    this.AddObstacleButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 0, (byte) 0, (byte) 0, (byte) 0));
    this.RemoveObstacleButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 44, (byte) 0, (byte) 0, (byte) 0));
    this.AddRobotButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 0, (byte) 0, (byte) 0, (byte) 0));
  }

  private void AddObstacleButton_Click(object sender, RoutedEventArgs e)
  {
    this._addObstacleEnabled = true;
    this._addRobotEnabled = false;
    this.AddObstacleButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 44, (byte) 0, (byte) 0, (byte) 0));
    this.RemoveObstacleButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 0, (byte) 0, (byte) 0, (byte) 0));
    this.AddRobotButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 0, (byte) 0, (byte) 0, (byte) 0));
  }

  private void ReceivedExpander_Expanded(object sender, RoutedEventArgs e)
  {
    this.MidRightGrid.Height = new GridLength(this.ReceivedTextBlock.ActualHeight + 54.0);
  }

  private void ReceivedExpander_Collapsed(object sender, RoutedEventArgs e)
  {
    this.MidRightGrid.Height = new GridLength(40.0);
  }

  private void SendExpander_Expanded(object sender, RoutedEventArgs e)
  {
    this.TopRightGrid.Height = new GridLength(this.SendTextBox.ActualHeight + 93.0);
  }

  private void SendExpander_Collapsed(object sender, RoutedEventArgs e)
  {
    this.TopRightGrid.Height = new GridLength(40.0);
  }

  private void CommandExpander_Expanded(object sender, RoutedEventArgs e)
  {
  }

  private void CommandExpander_Collapsed(object sender, RoutedEventArgs e)
  {
  }

  private void GenerateArenaButton_Click(object sender, RoutedEventArgs e)
  {
    try
    {
      this.GenerateNewArena(Convert.ToInt32(this.XTextBox.Text), Convert.ToInt32(this.YTextBox.Text));
    }
    catch (FormatException ex)
    {
      this.ShowToast("The dimensions must be whole numbers. Please try again.");
    }
  }

  public void SetInitialValues()
  {
    this.XTextBox.Text = string.Concat((object) Settings.Default.arenaWidth);
    this.YTextBox.Text = string.Concat((object) Settings.Default.arenaHeight);
    this.RobotSizeDropDownListBox.SelectedIndex = Settings.Default.robotSize - 1;
    this.DirectionTextBox.Text = string.Concat((object) Settings.Default.robotAngle);
  }

  public void GenerateNewArena(int width, int height)
  {
    this._localGridLayout = new int[width, height];
    Grid grid = new Grid();
    grid.Background = (Brush) new SolidColorBrush(Colors.Transparent);
    grid.Name = "ArenaMap";
    Grid element1 = grid;
    for (int index = 0; index < width; ++index)
    {
      ColumnDefinition columnDefinition = new ColumnDefinition()
      {
        Width = new GridLength(50.0)
      };
      element1.ColumnDefinitions.Add(columnDefinition);
    }
    for (int index = 0; index < height; ++index)
    {
      RowDefinition rowDefinition = new RowDefinition()
      {
        Height = new GridLength(50.0)
      };
      element1.RowDefinitions.Add(rowDefinition);
    }
    for (int index1 = 0; index1 < width; ++index1)
    {
      for (int index2 = 0; index2 < height; ++index2)
      {
        Button button = new Button();
        button.Style = (Style) this.FindResource((object) "ArenaSquareBtnStyle");
        button.Name = $"pos{(object) index1}_{(object) index2}";
        Button element2 = button;
        element2.MouseEnter += new MouseEventHandler(this.grid_MouseEnter);
        element2.MouseLeave += new MouseEventHandler(this.grid_MouseLeave);
        element2.Click += new RoutedEventHandler(this.grid_Click);
        element1.Children.Add((UIElement) element2);
        Grid.SetColumn((UIElement) element2, index1);
        Grid.SetRow((UIElement) element2, index2);
        this._localGridLayout[index1, index2] = 0;
      }
    }
    this.ArenaGrid.Children.Clear();
    this.ArenaGrid.Children.Add((UIElement) element1);
    this.RobotHolder.Children.Clear();
    this._robot = this.GenerateRobot();
    this.RobotHolder.Children.Add((UIElement) this._robot);
  }

  public void GenerateNewArena(int width, int height, string hexLayout, int[] robotPosition)
  {
    this._localGridLayout = new int[width, height];
    Grid grid = new Grid();
    grid.Background = (Brush) new SolidColorBrush(Colors.Transparent);
    grid.Name = "ArenaMap";
    Grid element1 = grid;
    for (int index = 0; index < width; ++index)
    {
      ColumnDefinition columnDefinition = new ColumnDefinition()
      {
        Width = new GridLength(50.0)
      };
      element1.ColumnDefinitions.Add(columnDefinition);
    }
    for (int index = 0; index < height; ++index)
    {
      RowDefinition rowDefinition = new RowDefinition()
      {
        Height = new GridLength(50.0)
      };
      element1.RowDefinitions.Add(rowDefinition);
    }
    string str1 = "";
    foreach (char ch in hexLayout)
    {
      string str2 = Convert.ToString(Convert.ToInt32(ch.ToString() ?? "", 16), 2);
      while (str2.Length != 4)
        str2 = 0.ToString() + str2;
      str1 += str2;
    }
    int index1 = 0;
    for (int index2 = 0; index2 < height; ++index2)
    {
      for (int index3 = 0; index3 < width; ++index3)
      {
        Button button1 = new Button();
        button1.Style = (Style) this.FindResource((object) "ArenaSquareBtnStyle");
        button1.Name = $"pos{(object) index3}_{(object) index2}";
        Button element2 = button1;
        element2.MouseEnter += new MouseEventHandler(this.grid_MouseEnter);
        element2.MouseLeave += new MouseEventHandler(this.grid_MouseLeave);
        element2.Click += new RoutedEventHandler(this.grid_Click);
        element1.Children.Add((UIElement) element2);
        Grid.SetColumn((UIElement) element2, index3);
        Grid.SetRow((UIElement) element2, index2);
        if (str1[index1].Equals('1'))
        {
          Button button2 = element2;
          Rectangle rectangle = new Rectangle();
          rectangle.Fill = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb(byte.MaxValue, (byte) 0, (byte) 0, (byte) 0));
          rectangle.Width = 50.0;
          rectangle.Height = 50.0;
          button2.Content = (object) rectangle;
          this._localGridLayout[index3, index2] = 1;
        }
        else
          this._localGridLayout[index3, index2] = 0;
        ++index1;
      }
    }
    this.ArenaGrid.Children.Clear();
    this.ArenaGrid.Children.Add((UIElement) element1);
    this.RobotHolder.Children.Clear();
    this._robot = this.GenerateRobot();
    this.RobotHolder.Children.Add((UIElement) this._robot);
    this._robot.Opacity = 1.0;
    this.RobotSizeDropDownListBox.SelectedIndex = robotPosition[3] - 1;
    this.DirectionTextBox.Text = string.Concat((object) robotPosition[2]);
    this._robot.Margin = new Thickness((double) (robotPosition[0] * 50), (double) (robotPosition[1] * 50), 0.0, 0.0);
  }

  private void grid_Click(object sender, RoutedEventArgs e)
  {
    Button button1 = (Button) sender;
    int[] numArray = this.RegexSolverForPosition(button1.Name);
    if (this._addObstacleEnabled)
    {
      Button button2 = button1;
      Rectangle rectangle = new Rectangle();
      rectangle.Fill = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb(byte.MaxValue, (byte) 0, (byte) 0, (byte) 0));
      rectangle.Width = button1.ActualWidth;
      rectangle.Height = button1.ActualWidth;
      button2.Content = (object) rectangle;
      this._localGridLayout[numArray[0], numArray[1]] = 1;
      if (!this._sendGrid)
        return;
      this.RunSendGridFormattingScript(new bool?(false), new bool?(true), new int[2]
      {
        numArray[0],
        numArray[1]
      });
    }
    else if (!this._addObstacleEnabled && !this._addRobotEnabled)
    {
      button1.Content = (object) "";
      this._localGridLayout[numArray[0], numArray[1]] = 0;
      if (!this._sendGrid)
        return;
      this.RunSendGridFormattingScript(new bool?(false), new bool?(false), new int[2]
      {
        numArray[0],
        numArray[1]
      });
    }
    else
    {
      if (!this._addRobotEnabled)
        return;
      int num = this.RobotSizeDropDownListBox.SelectedIndex + 1;
      Grid child = (Grid) this.ArenaGrid.Children[0];
      while (numArray[0] + num > child.ColumnDefinitions.Count)
        --numArray[0];
      while (numArray[1] + num > child.RowDefinitions.Count)
        --numArray[1];
      this._robot.Margin = new Thickness((double) (0 + numArray[0] * 50), (double) (0 + numArray[1] * 50), 0.0, 0.0);
      this._robot.Opacity = 1.0;
      this._robotPosition[0] = numArray[0];
      this._robotPosition[1] = numArray[1];
      if (!this._sendGrid)
        return;
      this.RunSendGridFormattingScript(new bool?(true), new bool?(false), (int[]) null);
    }
  }

  private void grid_MouseLeave(object sender, MouseEventArgs e)
  {
    Button mouseOveredButton = (Button) sender;
    this.RegexSolverForPosition(mouseOveredButton.Name);
    if (this._addObstacleEnabled)
    {
      mouseOveredButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 0, (byte) 0, (byte) 0, (byte) 0));
    }
    else
    {
      if (!this._addRobotEnabled)
        return;
      foreach (Control control in this.FindGridsToHighlight((Grid) this.ArenaGrid.Children[0], mouseOveredButton))
        control.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 0, (byte) 0, (byte) 0, (byte) 0));
    }
  }

  private void grid_MouseEnter(object sender, MouseEventArgs e)
  {
    Button mouseOveredButton = (Button) sender;
    if (this._addObstacleEnabled)
    {
      mouseOveredButton.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 99, (byte) 0, (byte) 0, (byte) 0));
    }
    else
    {
      if (!this._addRobotEnabled)
        return;
      foreach (Control control in this.FindGridsToHighlight((Grid) this.ArenaGrid.Children[0], mouseOveredButton))
        control.Background = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb(byte.MaxValue, byte.MaxValue, (byte) 196, (byte) 0));
    }
  }

  private void RunSendGridFormattingScript(bool? posTgridF, bool? addObstacle, int[] gridPosition)
  {
    try
    {
      string path = $"{System.IO.Path.GetDirectoryName(Assembly.GetEntryAssembly().Location)}\\scripts\\{Settings.Default.scriptFile}";
      File.ReadAllText(path);
      CompilerResults compilerResults = new CSharpCodeProvider().CompileAssemblyFromFile(new CompilerParameters()
      {
        GenerateExecutable = false,
        GenerateInMemory = true
      }, path);
      object[] parameters = new object[5]
      {
        (object) this._localGridLayout,
        (object) this._robotPosition,
        (object) posTgridF,
        (object) addObstacle,
        (object) gridPosition
      };
      foreach (object error in (CollectionBase) compilerResults.Errors)
        Console.WriteLine(error);
      string textToSend = (string) compilerResults.CompiledAssembly.GetExportedTypes()[0].GetMethod("MainScript").Invoke((object) null, parameters);
      this.Dispatcher.Invoke((Action) (() =>
      {
        string text = this.SendTextBox.Text;
        this.SendTextBox.Text = textToSend;
        this.SendBtn_Click((object) null, (RoutedEventArgs) null);
        this.SendTextBox.Text = text;
      }));
    }
    catch (Exception ex)
    {
      this.Dispatcher.Invoke((Action) (() => this.ShowToast("There might be some problems with your script. =/")));
    }
  }

  private int[] RegexSolverForPosition(string pos)
  {
    pos = new Regex(nameof (pos)).Replace(pos, "");
    string[] strArray = new Regex("_").Split(pos);
    return new int[2]
    {
      int.Parse(strArray[0]),
      int.Parse(strArray[1])
    };
  }

  private List<Button> FindGridsToHighlight(Grid grid, Button mouseOveredButton)
  {
    int[] position = this.RegexSolverForPosition(mouseOveredButton.Name);
    List<Button> gridsToHighlight = new List<Button>();
    int num = this.RobotSizeDropDownListBox.SelectedIndex + 1;
    while (position[0] + num > grid.ColumnDefinitions.Count)
      --position[0];
    while (position[1] + num > grid.RowDefinitions.Count)
      --position[1];
    for (int i = 0; i < num; i++)
    {
      for (int j = 0; j < num; j++)
      {
        Button button = (Button) grid.Children.Cast<UIElement>().First<UIElement>((Func<UIElement, bool>) (f => Grid.GetRow(f) == position[1] + j && Grid.GetColumn(f) == position[0] + i));
        gridsToHighlight.Add(button);
      }
    }
    return gridsToHighlight;
  }

  public Grid GenerateRobot()
  {
    Grid grid = new Grid();
    grid.Width = 50.0;
    grid.Height = 50.0;
    grid.IsHitTestVisible = false;
    grid.Opacity = 0.0;
    Grid robot = grid;
    Rectangle rectangle = new Rectangle();
    rectangle.Fill = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 155, byte.MaxValue, (byte) 109, (byte) 0));
    rectangle.Width = robot.Width;
    rectangle.Height = robot.Height;
    rectangle.Stroke = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb(byte.MaxValue, byte.MaxValue, (byte) 143, (byte) 0));
    Rectangle element1 = rectangle;
    PointCollection pointCollection = new PointCollection()
    {
      new System.Windows.Point(robot.Width - 0.2 * robot.Width, robot.Height - 0.1 * robot.Height),
      new System.Windows.Point(0.2 * robot.Width, robot.Height - 0.1 * robot.Height),
      new System.Windows.Point(robot.Width / 2.0, 0.1 * robot.Height)
    };
    Polygon polygon = new Polygon();
    polygon.Points = pointCollection;
    polygon.Fill = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb((byte) 155, (byte) 2, (byte) 136, (byte) 209));
    Polygon element2 = polygon;
    int num = this.RobotSizeDropDownListBox.SelectedIndex + 1;
    robot.RenderTransform = (Transform) new ScaleTransform((double) num, (double) num);
    robot.Children.Add((UIElement) element1);
    robot.Children.Add((UIElement) element2);
    return robot;
  }

  public void CommandInterpreter(string command)
  {
    string angleString = "";
    try
    {
      this.Dispatcher.Invoke((Action) (() => angleString = this.DirectionTextBox.Text));
    }
    catch (TaskCanceledException ex)
    {
    }
    int angle = int.Parse(angleString);
    bool flag = false;
    if (command.Equals(Settings.Default.turnLeft))
    {
      angle -= 90;
      while (angle < 0)
        angle = 360 + angle;
      flag = true;
      this.Dispatcher.Invoke((Action) (() =>
      {
        this.DirectionTextBox.Text = string.Concat((object) angle);
        this.CommandLog.AppendLine("Turn Left");
      }));
    }
    else if (command.Equals(Settings.Default.turnRight))
    {
      flag = true;
      this.Dispatcher.Invoke((Action) (() =>
      {
        this.DirectionTextBox.Text = string.Concat((object) (double.Parse(this.DirectionTextBox.Text) + 90.0));
        this.CommandLog.AppendLine("Turn Right");
      }));
    }
    else if (command.Equals(Settings.Default.forward))
    {
      flag = true;
      switch (angle)
      {
        case 0:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left;
            margin = this._robot.Margin;
            double top = margin.Top - 50.0;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          --this._robotPosition[1];
          break;
        case 90:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left + 50.0;
            margin = this._robot.Margin;
            double top = margin.Top;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          ++this._robotPosition[0];
          break;
        case 180:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left;
            margin = this._robot.Margin;
            double top = margin.Top + 50.0;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          ++this._robotPosition[1];
          break;
        case 270:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left - 50.0;
            margin = this._robot.Margin;
            double top = margin.Top;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          --this._robotPosition[0];
          break;
      }
      this.Dispatcher.Invoke((Action) (() => this.CommandLog.AppendLine("Forward")));
      if (this._sendGrid)
        this.RunSendGridFormattingScript(new bool?(true), new bool?(false), (int[]) null);
    }
    else if (command.Equals(Settings.Default.reverse))
    {
      flag = true;
      switch (angle)
      {
        case 0:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left;
            margin = this._robot.Margin;
            double top = margin.Top + 50.0;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          ++this._robotPosition[1];
          break;
        case 90:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left - 50.0;
            margin = this._robot.Margin;
            double top = margin.Top;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          --this._robotPosition[0];
          break;
        case 180:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left;
            margin = this._robot.Margin;
            double top = margin.Top - 50.0;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          --this._robotPosition[1];
          break;
        case 270:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left + 50.0;
            margin = this._robot.Margin;
            double top = margin.Top;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          ++this._robotPosition[0];
          break;
      }
      this.Dispatcher.Invoke((Action) (() => this.CommandLog.AppendLine("Reverse")));
      if (this._sendGrid)
        this.RunSendGridFormattingScript(new bool?(true), new bool?(false), (int[]) null);
    }
    else if (command.Equals(Settings.Default.strafeLeft))
    {
      flag = true;
      switch (angle)
      {
        case 0:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left - 50.0;
            margin = this._robot.Margin;
            double top = margin.Top;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          --this._robotPosition[0];
          break;
        case 90:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left;
            margin = this._robot.Margin;
            double top = margin.Top - 50.0;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          --this._robotPosition[1];
          break;
        case 180:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left + 50.0;
            margin = this._robot.Margin;
            double top = margin.Top;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          ++this._robotPosition[0];
          break;
        case 270:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left;
            margin = this._robot.Margin;
            double top = margin.Top + 50.0;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          ++this._robotPosition[1];
          break;
      }
      this.Dispatcher.Invoke((Action) (() => this.CommandLog.AppendLine("Strafe Left")));
      if (this._sendGrid)
        this.RunSendGridFormattingScript(new bool?(true), new bool?(false), (int[]) null);
    }
    else if (command.Equals(Settings.Default.strafeRight))
    {
      flag = true;
      switch (angle)
      {
        case 0:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left + 50.0;
            margin = this._robot.Margin;
            double top = margin.Top;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          ++this._robotPosition[0];
          break;
        case 90:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left;
            margin = this._robot.Margin;
            double top = margin.Top + 50.0;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          ++this._robotPosition[1];
          break;
        case 180:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left - 50.0;
            margin = this._robot.Margin;
            double top = margin.Top;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          --this._robotPosition[0];
          break;
        case 270:
          this.Dispatcher.Invoke((Action) (() =>
          {
            Grid robot = this._robot;
            Thickness margin = this._robot.Margin;
            double left = margin.Left;
            margin = this._robot.Margin;
            double top = margin.Top - 50.0;
            Thickness thickness = new Thickness(left, top, 0.0, 0.0);
            robot.Margin = thickness;
          }));
          --this._robotPosition[1];
          break;
      }
      this.Dispatcher.Invoke((Action) (() => this.CommandLog.AppendLine("Strafe Right")));
      if (this._sendGrid)
        this.RunSendGridFormattingScript(new bool?(true), new bool?(false), (int[]) null);
    }
    else if (command.Equals(Settings.Default.startExplore))
    {
      flag = true;
      this.Dispatcher.Invoke((Action) (() => this.CommandLog.AppendLine("Begin Exploration")));
    }
    else if (command.Equals(Settings.Default.startFastest))
    {
      flag = true;
      this.Dispatcher.Invoke((Action) (() => this.CommandLog.AppendLine("Begin Fastest Path")));
    }
    else if (command.Equals(Settings.Default.sendArena))
    {
      this.RunSendGridFormattingScript(new bool?(), new bool?(), (int[]) null);
      flag = true;
      this.Dispatcher.Invoke((Action) (() => this.CommandLog.AppendLine("Send Arena Info")));
    }
    if (!flag)
      return;
    this.Dispatcher.Invoke((Action) (() =>
    {
      this.CommandLogTextBlock.Text = this.CommandLog.ToString();
      this.CommandSV.ScrollToBottom();
      this._ecw.sv.ScrollToBottom();
      if (!this._ecw.IsVisible)
        return;
      this._ecw.CmdTextBlock.Text = this.CommandLogTextBlock.Text;
    }));
  }

  public void StartBluetoothListen()
  {
    try
    {
      BluetoothListener bluetoothListener = new BluetoothListener(BluetoothService.SerialPort);
      AsyncCallback callback = (AsyncCallback) (ar =>
      {
        this._btClient = bluetoothListener.EndAcceptBluetoothClient((IAsyncResult) ar);
        this._btAddress = this._btClient.RemoteEndPoint.Address;
        this.Dispatcher.Invoke((Action) (() =>
        {
          this.BtLabel1.Opacity = 0.0;
          this.BtLabel2.Opacity = 1.0;
          this.BtLabel2.Content = (object) ("CONNECTED TO " + this._btClient.RemoteMachineName);
        }));
        this._stream = (NetworkStream) this._btClient.GetStream();
        this._btData.Stream = this._stream;
        this._btData.BtClient = this._btClient;
        this._stream.BeginRead(this._readBuffer, 0, this._readBuffer.Length, new AsyncCallback(this.BluetoothReceiverCallback), (object) this._btData);
      });
      bluetoothListener.Start();
      bluetoothListener.BeginAcceptBluetoothClient((AsyncCallback) callback, (object) null);
    }
    catch (PlatformNotSupportedException ex)
    {
    }
    catch (SocketException ex)
    {
    }
  }

  public void BluetoothReceiverCallback(IAsyncResult ar)
  {
    BluetoothData asyncState = (BluetoothData) ar.AsyncState;
    try
    {
      string receivedString = Encoding.Default.GetString(this._readBuffer, 0, asyncState.Stream.EndRead(ar));

      this.Dispatcher.Invoke((Action) (() =>
      {
        this.ReceivedLog.AppendLine(receivedString);
        this.ReceivedTextBlock.Text = this.ReceivedLog.ToString();
        this.ReceivedSV.ScrollToBottom();
        this._erw.sv.ScrollToBottom();
        if (this._erw.IsVisible)
          this._erw.ReceivedTextBlock.Text = this.ReceivedTextBlock.Text;
      }));

      // First pass plain (non-JSON) tokens to CommandInterpreter
      string trimmed = receivedString.Trim();
      if (!trimmed.StartsWith("{"))
          this.CommandInterpreter(trimmed);

      // Extract all JSON objects in the received string using Regex
      MatchCollection jsonMatches = Regex.Matches(receivedString, @"\{.*?\}");
      foreach (Match match in jsonMatches)
      {
          string jsonStr = match.Value;
          try
          {
            JavaScriptSerializer serializer = new JavaScriptSerializer();
            TabletUpdate parsedData = serializer.Deserialize<TabletUpdate>(jsonStr);

            if (parsedData.Type != null)
            {
                this.Dispatcher.Invoke((Action) (() =>
                {
                  if (parsedData.Type == "Obstacle")
                  {
                    // Invert Y coordinate since AMD uses Top-Left origin but Android uses Bottom-Left (20x20 grid)
                    int amdY = 19 - parsedData.Y;
                    this._localGridLayout[parsedData.X, amdY] = 1;

                    Grid innerGrid = (Grid) this.ArenaGrid.Children[0];
                    Button targetGridButton = (Button) innerGrid.Children.Cast<UIElement>().First(f => Grid.GetRow(f) == amdY && Grid.GetColumn(f) == parsedData.X);

                    Rectangle rectangle = new Rectangle();
                    rectangle.Fill = (Brush) new SolidColorBrush(System.Windows.Media.Color.FromArgb(byte.MaxValue, 0, 0, 0));
                    rectangle.Width = targetGridButton.ActualWidth;
                    rectangle.Height = targetGridButton.ActualWidth;
                    targetGridButton.Content = (object) rectangle;
                  }
                  else if (parsedData.Type == "RemoveObstacle")
                  {
                    int amdY = 19 - parsedData.Y;
                    this._localGridLayout[parsedData.X, amdY] = 0;

                    Grid innerGrid = (Grid) this.ArenaGrid.Children[0];
                    Button targetGridButton = (Button) innerGrid.Children.Cast<UIElement>().First(f => Grid.GetRow(f) == amdY && Grid.GetColumn(f) == parsedData.X);
                    targetGridButton.Content = (object) "";
                  }
                  else if (parsedData.Type == "Robot")
                  {
                    this._robotPosition[0] = parsedData.X;
                    this._robotPosition[1] = parsedData.Y;
                    this._robotPosition[2] = parsedData.Direction;

                    // Invert Y coordinate for 3x3 robot on 20x20 grid
                    int amdY = 20 - 3 - parsedData.Y;

                    this._robot.Margin = new Thickness((double) (0 + parsedData.X * 50), (double) (0 + amdY * 50), 0.0, 0.0);
                    this._robot.Opacity = 1.0;
                    this.DirectionTextBox.Text = parsedData.Direction.ToString();
                  }
                  else if (parsedData.Type == "RemoveRobot")
                  {
                    this._robot.Opacity = 0.0;
                  }
                }));
            }
            else
            {
                this.CommandInterpreter(jsonStr);
            }
          }
          catch (Exception)
          {
            this.CommandInterpreter(jsonStr);
          }
      }

      asyncState.Stream.BeginRead(this._readBuffer, 0, this._readBuffer.Length, new AsyncCallback(this.BluetoothReceiverCallback), (object) asyncState);
    }
    catch (ObjectDisposedException ex)
    {
    }
  }

  public async Task ShowToast(string message)
  {
    this.Toast.Content = (object) message;
    DoubleAnimation animation1 = new DoubleAnimation(0.0, 1.0, new Duration(new TimeSpan(0, 0, 0, 0, 300)));
    this.Toast.BeginAnimation(UIElement.OpacityProperty, (AnimationTimeline) animation1);
    await Task.Delay(5000);
    DoubleAnimation animation2 = new DoubleAnimation(this.Toast.Opacity, 0.0, new Duration(new TimeSpan(0, 0, 0, 0, 300)));
    this.Toast.BeginAnimation(UIElement.OpacityProperty, (AnimationTimeline) animation2);
  }
}
