main: (x: u8, y: u8)
{
    z: u8 = x + y;
    x = y + z;
    return x + x;
}
