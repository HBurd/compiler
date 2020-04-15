main: ()
{
    x := 64;
    y := x + 1;
    x = x + y;

    bingo: () {
        z := 17;
    }

    x = x + y;
    
    return x;
}
