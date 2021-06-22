exit: (code: u32);

exit3: (code: u32)
{
    exit(code + 3);
    return;
}

main: () -> u32
{
    exit3(5);
    return 1;
}
