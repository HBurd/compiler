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

    return 0;
}
