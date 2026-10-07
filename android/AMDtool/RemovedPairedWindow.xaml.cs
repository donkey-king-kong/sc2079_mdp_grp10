using InTheHand.Net.Bluetooth;
using InTheHand.Net.Sockets;
using System;
using System.Collections;
using System.Windows;
using System.Windows.Controls;

#nullable disable
namespace AMDtool;

public partial class RemovedPairedWindow : Window
{
  private BluetoothDeviceInfo[] devices;

  public RemovedPairedWindow()
  {
    this.InitializeComponent();
    this.RemoveBtn.Click += new RoutedEventHandler(this.RemoveBtn_Click);
    this.CloseBtn.Click += new RoutedEventHandler(this.CloseBtn_Click);
    this.SelectAllBtn.Click += new RoutedEventHandler(this.SelectAllBtn_Click);
    this.ListDevices();
  }

  private void SelectAllBtn_Click(object sender, RoutedEventArgs e) => this.DevicesList.SelectAll();

  private void CloseBtn_Click(object sender, RoutedEventArgs e) => this.Close();

  private void RemoveBtn_Click(object sender, RoutedEventArgs e)
  {
    IList selectedItems = this.DevicesList.SelectedItems;
    foreach (object obj in (IEnumerable) selectedItems)
      BluetoothSecurity.RemoveDevice(this.devices[selectedItems.IndexOf(obj)].DeviceAddress);
    this.ListDevices();
  }

  private void ListDevices()
  {
    try
    {
      this.DevicesList.Items.Clear();
      this.devices = new BluetoothClient().DiscoverDevices(99, true, false, false, false);
      foreach (BluetoothDeviceInfo device in this.devices)
        this.DevicesList.Items.Add((object) device.DeviceName);
    }
    catch (PlatformNotSupportedException ex)
    {
    }
  }
}
