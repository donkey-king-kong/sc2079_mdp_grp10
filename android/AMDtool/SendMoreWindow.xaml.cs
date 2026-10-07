using AMDtool.Properties;
using InTheHand.Net.Sockets;
using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows;
using System.Windows.Controls;

#nullable disable
namespace AMDtool;

public partial class SendMoreWindow : Window
{
  public BluetoothClient BtClient { get; set; }

  public SendMoreWindow()
  {
    this.InitializeComponent();
    this.SendBtn.Click += new RoutedEventHandler(this.SendBtn_Click);
    this.DelayBtn.Click += new RoutedEventHandler(this.DelayBtn_Click);
    this.ClearBtn.Click += new RoutedEventHandler(this.ClearBtn_Click);
    this.c1btn.Click += new RoutedEventHandler(this.C1btn_Click);
    this.c2btn.Click += new RoutedEventHandler(this.C2btn_Click);
    this.c3btn.Click += new RoutedEventHandler(this.C3btn_Click);
    this.c4btn.Click += new RoutedEventHandler(this.C4btn_Click);
    this.c5btn.Click += new RoutedEventHandler(this.C5btn_Click);
    this.c6btn.Click += new RoutedEventHandler(this.C6btn_Click);
    this.c7btn.Click += new RoutedEventHandler(this.C7btn_Click);
    this.c8btn.Click += new RoutedEventHandler(this.C8btn_Click);
    this.c9btn.Click += new RoutedEventHandler(this.C9btn_Click);
    this.StatusDemoBtn.Click += new RoutedEventHandler(this.StatusDemoBtn_Click);
    this.SendSettingBtn.Click += new RoutedEventHandler(this.SendSettingBtn_Click);
  }

  private void SendSettingBtn_Click(object sender, RoutedEventArgs e)
  {
    SettingsWindow settingsWindow = new SettingsWindow();
    settingsWindow.Show();
    settingsWindow.Navigator.SelectedIndex = 1;
  }

  private void StatusDemoBtn_Click(object sender, RoutedEventArgs e)
  {
    bool[] flagArray = new bool[10];
    long msgEnable = Settings.Default.msgEnable;
    for (int index = 0; index < 10; ++index)
    {
      flagArray[index] = msgEnable % 10L == 1L;
      msgEnable /= 10L;
    }
    List<string> stringList = new List<string>();
    if (flagArray[0]) stringList.Add(Settings.Default.msg1);
    if (flagArray[1]) stringList.Add(Settings.Default.msg2);
    if (flagArray[2]) stringList.Add(Settings.Default.msg3);
    if (flagArray[3]) stringList.Add(Settings.Default.msg4);
    if (flagArray[4]) stringList.Add(Settings.Default.msg5);
    if (flagArray[5]) stringList.Add(Settings.Default.msg6);
    if (flagArray[6]) stringList.Add(Settings.Default.msg7);
    if (flagArray[7]) stringList.Add(Settings.Default.msg8);
    if (flagArray[8]) stringList.Add(Settings.Default.msg9);
    if (flagArray[9]) stringList.Add(Settings.Default.msg10);
    try
    {
      NetworkStream stream = (NetworkStream) this.BtClient.GetStream();
      Random random = new Random();
      for (int index = 0; index < Settings.Default.msgNum; ++index)
      {
        byte[] bytes = Encoding.Default.GetBytes(stringList[random.Next(0, stringList.Count)]);
        stream.Write(bytes, 0, bytes.Length);
        Thread.Sleep(Settings.Default.delayDuration);
      }
    }
    catch (InvalidOperationException ex) { }
    catch (NullReferenceException ex) { Console.WriteLine("Null reference"); }
    catch (IOException ex) { }
  }

  private void C9btn_Click(object sender, RoutedEventArgs e)
  {
    bool? isChecked = this.SendInstantly.IsChecked;
    bool flag = true;
    if ((isChecked.GetValueOrDefault() == flag ? (isChecked.HasValue ? 1 : 0) : 0) != 0)
    {
      this.SendTextBox.Text = Settings.Default.custom9;
      this.InstantSend();
    }
    else
      this.SendTextBox.Text += Settings.Default.custom9;
  }

  private void C8btn_Click(object sender, RoutedEventArgs e)
  {
    bool? isChecked = this.SendInstantly.IsChecked;
    bool flag = true;
    if ((isChecked.GetValueOrDefault() == flag ? (isChecked.HasValue ? 1 : 0) : 0) != 0)
    {
      this.SendTextBox.Text = Settings.Default.custom8;
      this.InstantSend();
    }
    else
      this.SendTextBox.Text += Settings.Default.custom8;
  }

  private void C7btn_Click(object sender, RoutedEventArgs e)
  {
    bool? isChecked = this.SendInstantly.IsChecked;
    bool flag = true;
    if ((isChecked.GetValueOrDefault() == flag ? (isChecked.HasValue ? 1 : 0) : 0) != 0)
    {
      this.SendTextBox.Text = Settings.Default.custom7;
      this.InstantSend();
    }
    else
      this.SendTextBox.Text += Settings.Default.custom7;
  }

  private void C6btn_Click(object sender, RoutedEventArgs e)
  {
    bool? isChecked = this.SendInstantly.IsChecked;
    bool flag = true;
    if ((isChecked.GetValueOrDefault() == flag ? (isChecked.HasValue ? 1 : 0) : 0) != 0)
    {
      this.SendTextBox.Text = Settings.Default.custom6;
      this.InstantSend();
    }
    else
      this.SendTextBox.Text += Settings.Default.custom6;
  }

  private void C5btn_Click(object sender, RoutedEventArgs e)
  {
    bool? isChecked = this.SendInstantly.IsChecked;
    bool flag = true;
    if ((isChecked.GetValueOrDefault() == flag ? (isChecked.HasValue ? 1 : 0) : 0) != 0)
    {
      this.SendTextBox.Text = Settings.Default.custom5;
      this.InstantSend();
    }
    else
      this.SendTextBox.Text += Settings.Default.custom5;
  }

  private void C4btn_Click(object sender, RoutedEventArgs e)
  {
    bool? isChecked = this.SendInstantly.IsChecked;
    bool flag = true;
    if ((isChecked.GetValueOrDefault() == flag ? (isChecked.HasValue ? 1 : 0) : 0) != 0)
    {
      this.SendTextBox.Text = Settings.Default.custom4;
      this.InstantSend();
    }
    else
      this.SendTextBox.Text += Settings.Default.custom4;
  }

  private void C3btn_Click(object sender, RoutedEventArgs e)
  {
    bool? isChecked = this.SendInstantly.IsChecked;
    bool flag = true;
    if ((isChecked.GetValueOrDefault() == flag ? (isChecked.HasValue ? 1 : 0) : 0) != 0)
    {
      this.SendTextBox.Text = Settings.Default.custom3;
      this.InstantSend();
    }
    else
      this.SendTextBox.Text += Settings.Default.custom3;
  }

  private void C2btn_Click(object sender, RoutedEventArgs e)
  {
    bool? isChecked = this.SendInstantly.IsChecked;
    bool flag = true;
    if ((isChecked.GetValueOrDefault() == flag ? (isChecked.HasValue ? 1 : 0) : 0) != 0)
    {
      this.SendTextBox.Text = Settings.Default.custom2;
      this.InstantSend();
    }
    else
      this.SendTextBox.Text += Settings.Default.custom2;
  }

  private void C1btn_Click(object sender, RoutedEventArgs e)
  {
    bool? isChecked = this.SendInstantly.IsChecked;
    bool flag = true;
    if ((isChecked.GetValueOrDefault() == flag ? (isChecked.HasValue ? 1 : 0) : 0) != 0)
    {
      this.SendTextBox.Text = Settings.Default.custom1;
      this.InstantSend();
    }
    else
      this.SendTextBox.Text += Settings.Default.custom1;
  }

  private void InstantSend()
  {
    try
    {
      NetworkStream stream = this.BtClient.GetStream();
      byte[] bytes = Encoding.Default.GetBytes(this.SendTextBox.Text);
      byte[] buffer = bytes;
      int length = bytes.Length;
      ((Stream) stream).Write(buffer, 0, length);
    }
    catch (InvalidOperationException ex) { }
    catch (NullReferenceException ex) { Console.WriteLine("Null reference"); }
    catch (IOException ex) { }
  }

  private void ClearBtn_Click(object sender, RoutedEventArgs e) => this.SendTextBox.Text = "";

  private void DelayBtn_Click(object sender, RoutedEventArgs e)
  {
    TextBox sendTextBox = this.SendTextBox;
    sendTextBox.Text = $"{sendTextBox.Text}\n{Settings.Default.delay}\n";
    this.SendTextBox.Focus();
    this.SendTextBox.Select(this.SendTextBox.Text.Length, 0);
  }

  private void SendBtn_Click(object sender, RoutedEventArgs e)
  {
    try
    {
      NetworkStream stream = (NetworkStream) this.BtClient.GetStream();
      foreach (string splitIntoLine in this.SplitIntoLines(this.SendTextBox.Text))
      {
        byte[] bytes = Encoding.Default.GetBytes(splitIntoLine);
        stream.Write(bytes, 0, bytes.Length);
        Thread.Sleep(Settings.Default.delayDuration);
      }
    }
    catch (InvalidOperationException ex) { }
    catch (NullReferenceException ex) { Console.WriteLine("Null reference"); }
    catch (IOException ex) { }
  }

  private string[] SplitIntoLines(string text)
  {
    return new Regex($"\n{Settings.Default.delay}\n").Split(text);
  }
}
