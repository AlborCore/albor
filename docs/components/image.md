# Image

A leaf that draws retained pixels into the bounds its parent arranged. The source is a
decoded `image` handle, either an embedded asset or a bound property, so the same picture is
uploaded once and reused across frames (§6.4).

## Properties

| property | type | notes |
| --- | --- | --- |
| `source` | `image` | the retained pixels; written `@image("path")` or bound to a property |
| `description` | `string` | the accessible name; an empty description falls back to the author `id` |
| `width` | `float` | the explicit width in logical units; accepts `px`; `0px` keeps the natural width |
| `height` | `float` | the explicit height in logical units; accepts `px`; `0px` keeps the natural height |

Plus the [common properties](index.md#common-properties).

## Signals

None — an image is static.

## Children

None — an image is a leaf.

## Example

```text
component Header {
    Image {
        id: logo;
        source: @image("assets/logo.png");
        description: @tr("Company logo");
        width: 160px;
        height: 48px;
    }
}
```

An asset path is resolved beside the `.ui` file that writes it and embedded at build time,
so a shipped application does not read the picture from disk. Embedded `@image` assets must
be PNG files; a bound `image` handle may carry any pixels the application registers. The
preview resolves the same path beside the source file it is showing. The dev runtime
resolves a serialized path against the asset root its service supplies, and uses the path
as written when no root is set. A source may also be bound:

```text
component Profile {
    in property<image> avatar;
    in property<string> user_name;

    Image {
        id: avatar_picture;
        source: avatar;
        description: user_name;
    }
}
```

## Sizing

With both dimensions left at `0px`, the image measures to the source's pixel extent. Setting
only one dimension keeps the other natural; setting both reserves exactly that rectangle.
A negative value such as `-12.0` is treated as zero, which keeps the natural extent.
The arranged bounds are filled as they are: the image stretches to the rectangle the
parent gives it and does not preserve its aspect ratio.

## Semantics

The image announces the `image` role, its author-assigned `id`, its description (or the `id`
when no description is written) and its current bounds. An image with no source, or one
whose source carries no pixels, is announced as disabled. It exposes no actions and is not a
keyboard focus stop.
