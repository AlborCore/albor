# Expressions and bindings

Every right-hand side in a `.ui` file — a property default, an element assignment, a handler
value, a condition — is an expression. The expression grammar is small, fully typed and
closed: what is not on this page does not compile.

## Literals

```text
42                      // int
0.5                     // float
true                    // bool
"Hello"                 // string
@tr("Increment")        // a translatable string ([i18n](../concepts/i18n.md))
#0A82FF                 // colour: six hex digits, opaque
#0A82FFCC               // colour: eight hex digits, with alpha
12px                    // length: logical pixels, scale with display density
12phx                   // length: physical device pixels, do not scale
```

!!! note "Which unit when"
    `px` is almost always the right choice: it is the unit the layout works in, and it
    scales with the display. `phx` exists for work that must address device pixels exactly —
    a 1-pixel hairline, for instance.

## Names

A bare identifier resolves in a fixed order — the innermost scope wins:

1. a signal parameter of the handler you are in
2. a function parameter of the function you are in
3. a repeat item (or repeat index) of the `Repeat` you are in
4. a property of the component
5. a module-scoped value (`global`, the last resort)

An unknown name is a compile error, not a silent lookup at runtime.

## Operators

| operator | accepts | yields |
| --- | --- | --- |
| `+` | numbers; string `+` anything (concatenation) | number / string |
| `-` `*` `/` `%` | numbers | number |
| `==` `!=` | two values of the same type | bool |
| `<` `<=` `>` `>=` | numbers | bool |
| `&&` `\|\|` | bool | bool |
| `!` | bool | bool |

Nothing widens implicitly: `int` and `float` never convert on their own, so
`string(click_count)` is how a label shows an integer.

```text
Label { text: @tr("Count: ") + string(click_count); }
Label { text: string(level * 100.0) + "%"; }
Canvas { width: 380px; height: 20px; }
```

## Conversions

Four explicit conversions exist; each is a call form:

```text
string(some_float)     // float  -> string
int(some_float)        // float  -> int   (truncating)
float(some_int)        // int    -> float
bool(some_int)         // int    -> bool
```

A conversion of something the checker cannot type is refused rather than guessed.

## Colour operations

Three built-in functions work on colours, each taking a colour and an amount and returning a
colour:

```text
transparentize(#0A82FF, 0.5)   // alpha moves toward zero by the fraction — de-emphasis
alpha(#0A82FF, 0.5)            // alpha is multiplied — a dimming overlay
alpha_set(#0A82FF, 0.5)        // alpha is replaced — an explicit opacity
```

The amount may be any number expression; it is clamped where the colour clamps.

## Translations

`@tr("literal")` marks a string as translatable. It takes exactly one string literal — a
key with holes is written as concatenation of translated pieces:

```text
Label { text: @tr("Hello, ") + user_name + "!"; }
```

## Functions

A file may declare a pure named computation and call it from any expression:

```text
fn tick_step(low: float, high: float): float {
    return (high - low) / 10.0;
}

component Axis {
    in property<float> low;
    in property<float> high;

    Label { text: string(tick_step(low, high)); }
}
```

A function reads **only its parameters** — no properties, no globals, no other functions of
the component scope — which is exactly what keeps a binding from becoming a hidden cycle.
Declared functions shadow built-ins of the same name; arity and argument types are checked.

## Record construction

A declared record is constructed by naming its fields:

```text
struct Frame { float width; float height; }

Frame { width: 16.0, height: 9.0 }    // both fields
Frame { width: 16.0 }                 // height is value-initialised
```

Named fields make the written order free; the compiler emits them in the record's declared
order, which is what a C++ designated initialiser requires. A field read is a dot access —
`frame.width` — and a field of an `inout` record property may be written in a handler
(`frame.width = 10.0;`), which rebuilds the record and sets it back in one step.

## Globals

A file may declare a module-scoped named value that every component in it reads:

```text
global palette_accent: color = #0A82FF;
global tick_step: float = 2.5;

component Axis {
    Label { foreground: palette_accent; text: string(tick_step); }
}
```

A global is **read-only** — there is no assignment form for it, and a handler that writes one
is a compile error. That is deliberate: a global is a shared constant, not a shared mutable
slot; anything a component writes belongs on an `inout` property of that component, or on a
command. A global may be any declared type, including a record, constructed the same way a
record construction is written anywhere else.

Globals resolve last in the [name order](#names), so a local property or parameter of the
same name shadows one.

## Assignment with a source: the binding form

An element assignment whose value is an expression is not a one-time copy — it is a
**binding**:

```text
Slider {
    value: level;                 // binds value to the property level
    on_changed: { level = value; } // the reverse direction, written by hand
}
```

The compiler records the properties the expression reads as declared dependencies. When one
of them changes, the binding re-evaluates **eagerly** and writes the new result through the
element's setter — no polling, no frame-long staleness. The evaluation model behind this is
covered in [Properties and bindings](../concepts/bindings.md).

A binding may also be a plain literal (`text: "press me";`), which simply sets the property
once at construction.
