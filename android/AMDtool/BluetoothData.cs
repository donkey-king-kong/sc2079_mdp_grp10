// Decompiled with JetBrains decompiler
// Type: AMDtool.BluetoothData
// Assembly: AMDtool, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null
// MVID: 8D4CC376-ADB2-4178-ABF8-2A3517123F8A
// Assembly location: C:\Users\user\Downloads\AMDTOOL updated 08_Oct_2015\AMDtool.exe

using InTheHand.Net.Sockets;
using System.Net.Sockets;

#nullable disable
namespace AMDtool;

internal class BluetoothData
{
  public NetworkStream Stream { get; set; }

  public BluetoothClient BtClient { get; set; }
}
