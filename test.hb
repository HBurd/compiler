main: (x: i32, y: i32)
{
    if x < y
    {
        while x > x
        {
            x = x + y;
        }
        return 1;
    }
    else
    {
        y = x + x;
    }

    return 0;
}
