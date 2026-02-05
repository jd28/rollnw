# Tutorial

## Hello

```go
from core.prelude import { println };

fn main() {
    println("Hello from Smalls");
}
```

## Collections

```go
import core.array as arr;
import core.map as m;

fn main() {
    var xs: array!(int) = { 1, 2, 3 };
    arr.push(xs, 4);

    var scores: map!(string, int) = { "a": 1 };
    m.set(scores, "b", 2);
}
```

## String formatting

```go
import core.string as str;

fn main() {
    var msg = str.format("x={} y={}", { "1", "2" });
}
```
