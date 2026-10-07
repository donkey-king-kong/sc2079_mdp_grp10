using InTheHand.Net.Bluetooth;
using InTheHand.Net.Sockets;
using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

#nullable disable
namespace AMDtool;

public partial class BluetoothWindow : Window
{
  public List<BluetoothDeviceInfo> Devices = new List<BluetoothDeviceInfo>();
  public BluetoothDeviceInfo SelectedDevice;
  public bool Cancel;

  public BluetoothWindow()
  {
    this.InitializeComponent();
    this.Style = (Style) this.FindResource((object) typeof (Window));
    this.DiscoverBtBtn.Click += new RoutedEventHandler(this.DiscoverBtBtn_Click);
    this.BtOkBtn.Click += new RoutedEventHandler(this.BtOkBtn_Click);
    this.BtCancelBtn.Click += new RoutedEventHandler(this.BtCancelBtn_Click);
    this.DevicesListView.MouseDoubleClick += new MouseButtonEventHandler(this.BtOkBtn_Click);
  }

  private void BtCancelBtn_Click(object sender, RoutedEventArgs e)
  {
    this.Cancel = true;
    this.Close();
  }

  private void BtOkBtn_Click(object sender, RoutedEventArgs e)
  {
    try
    {
      this.SelectedDevice = this.Devices[this.DevicesListView.SelectedIndex];
      this.Close();
    }
    catch (NullReferenceException ex)
    {
      this.ErrorLabel.Text = "Something went wrong. Don't worry it's not your fault, just scan and try again.";
    }
    catch (IndexOutOfRangeException ex)
    {
      this.ErrorLabel.Text = "Something went wrong. Don't worry it's not your fault, just scan and try again...";
    }
    catch (ArgumentOutOfRangeException ex)
    {
    }
  }

  private void DiscoverBtBtn_Click(object sender, RoutedEventArgs e)
  {
    try
    {
      this.DevicesListView.Items.Clear();
      this.Devices.Clear();
      BluetoothComponent bluetoothComponent = new BluetoothComponent();
      bluetoothComponent.DiscoverDevicesAsync(99, true, true, true, false, (object) null);
      bluetoothComponent.DiscoverDevicesProgress += (EventHandler<DiscoverDevicesEventArgs>) new EventHandler<DiscoverDevicesEventArgs>(this.x_DiscoverDevicesProgress);
      bluetoothComponent.DiscoverDevicesComplete += (EventHandler<DiscoverDevicesEventArgs>) new EventHandler<DiscoverDevicesEventArgs>(this.x_DiscoverDevicesComplete);
      this.ScanningImage.Opacity = 1.0;
      this.ScanningLabel.Opacity = 1.0;
    }
    catch (PlatformNotSupportedException ex)
    {
      Console.WriteLine("No bluetooth on this device.");
      this.ErrorLabel.Text = "Sorry, bluetooth isn't support on this PC. :(";
    }
  }

  private void x_DiscoverDevicesComplete(object sender, DiscoverDevicesEventArgs e)
  {
    this.ScanningImage.Opacity = 0.0;
    this.ScanningLabel.Opacity = 0.0;
  }

  private void x_DiscoverDevicesProgress(object sender, DiscoverDevicesEventArgs e)
  {
    foreach (BluetoothDeviceInfo device in e.Devices)
    {
      this.Devices.Add(device);
      this.DevicesListView.Items.Add((object) device.DeviceName);
    }
  }
}
