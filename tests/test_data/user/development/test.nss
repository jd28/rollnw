
const int GLOBAL = 1;

int thisisatest(int arg1, string arg2, int optional = 0);

int thisisatest(int arg1, string arg2, int optional)
{
    return IntToString(arg1) == arg2;
}

struct MyInt {
    int test;
};

struct ComplexType {
    int test;
    float f;
    itemproperty ip;
    struct MyInt myint;
};

struct ComplexType func(struct MyInt value);

void main()
{
    int a = 3, b, c = 1;
    struct MyInt test;
    test2.myint.test = 1;
    effect e;
    int t = 1;
    func(test).myint;
    ++thisisatest("hello", "world");
    --t++++;

    int a = ~b;

    if(this || that) {
        dosometing();
    }

    while(this && !another) {
        i -= 1;
    }

    if(i == 1) {
        i += 1;
    } else {
        i %= 1;
    }

    switch(i) {
        default:
        i++;
        --i;
        break;
        case 1: {
        i++;
        --i;
        }
        break;
        case 2: break;
        case 3: break;
        case 4: break;
    }

    do {
        i %= 3;
    } while (--x);

    while(i > 0) {
        IntToString(i);
    }

    for(i = 0; i < 10; ++i) {
        if(i==2) {
            break;
        } else {
            i *= 4;
        }
    }

    int tt = TRUE ? t++ : 0;
}
