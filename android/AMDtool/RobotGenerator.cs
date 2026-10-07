// Decompiled with JetBrains decompiler
// Type: AMDtool.RobotGenerator
// Assembly: AMDtool, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null
// MVID: 8D4CC376-ADB2-4178-ABF8-2A3517123F8A
// Assembly location: C:\Users\user\Downloads\AMDTOOL updated 08_Oct_2015\AMDtool.exe

using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Shapes;

#nullable disable
namespace AMDtool;

internal class RobotGenerator
{
  private int _robotSize;
  private int _xOffset;
  private int _yOffset;

  public SolidColorBrush BodyColour { get; set; }

  public SolidColorBrush PointerColour { get; set; }

  public int RobotSize
  {
    get => this._robotSize;
    set
    {
      this._robotSize = value;
      this._xOffset = this.GenerateXOffset(value);
      this._yOffset = this.GenerateYOffset(value);
    }
  }

  public int XOffset => this._xOffset;

  public int YOffset => this._yOffset;

  public RobotGenerator(SolidColorBrush bodyColour, SolidColorBrush pointerColour, int robotSize)
  {
    this.BodyColour = bodyColour;
    this.PointerColour = pointerColour;
    this.RobotSize = robotSize;
    this._xOffset = this.GenerateXOffset(robotSize);
    this._yOffset = this.GenerateYOffset(robotSize);
  }

  public RobotGenerator(int robotSize)
  {
    this.BodyColour = new SolidColorBrush(Color.FromArgb((byte) 102, byte.MaxValue, (byte) 0, (byte) 0));
    this.PointerColour = new SolidColorBrush(Color.FromArgb((byte) 102, byte.MaxValue, (byte) 180, (byte) 0));
    this.RobotSize = robotSize;
    this._xOffset = this.GenerateXOffset(robotSize);
    this._yOffset = this.GenerateYOffset(robotSize);
  }

  private int GenerateXOffset(int robotSize)
  {
    int xoffset = -700;
    for (int index = robotSize; index > 1; --index)
      xoffset += 50;
    return xoffset;
  }

  private int GenerateYOffset(int robotSize)
  {
    int yoffset = -950;
    for (int index = robotSize; index > 1; --index)
      yoffset += 50;
    return yoffset;
  }

  private int GenerateWidth()
  {
    int width = 50;
    for (int robotSize = this.RobotSize; robotSize > 1; --robotSize)
      width += 50;
    return width;
  }

  public Grid GenerateRobot()
  {
    Grid robot = new Grid();
    robot.Width = (double) this.GenerateWidth();
    Grid grid = robot;
    grid.Height = grid.Width;
    robot.IsHitTestVisible = false;
    Rectangle rectangle = new Rectangle();
    rectangle.Fill = (Brush) this.BodyColour;
    rectangle.Width = robot.Width;
    rectangle.Height = robot.Height;
    Rectangle element1 = rectangle;
    PointCollection pointCollection = new PointCollection()
    {
      new System.Windows.Point(0.1 * robot.Width, 0.2 * robot.Height),
      new System.Windows.Point(0.1 * robot.Width, robot.Height - 0.2 * robot.Height),
      new System.Windows.Point(robot.Width - 0.1 * robot.Width, robot.Height / 2.0)
    };
    Polygon polygon = new Polygon();
    polygon.Points = pointCollection;
    polygon.Fill = (Brush) this.PointerColour;
    Polygon element2 = polygon;
    robot.Children.Add((UIElement) element1);
    robot.Children.Add((UIElement) element2);
    return robot;
  }
}
