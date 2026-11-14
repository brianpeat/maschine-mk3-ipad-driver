// Renders printable ASCII in Menlo into an 8 x 16 one-bit font and writes Driver/Font8x16.h.
// Usage: swift tools/makefont.swift > Driver/Font8x16.h
import CoreGraphics
import CoreText
import Foundation

let cellW = 8, cellH = 16
let font = CTFontCreateWithName("Menlo-Regular" as CFString, 13, nil)
var out = "// 8 x 16 one-bit font, printable ASCII 32-126, rendered from Menlo by tools/makefont.swift.\n"
out += "// Each glyph is 16 rows, top first; bit 7 is the leftmost pixel.\n#pragma once\n#include <stdint.h>\n\n"
out += "static const uint8_t kFont8x16[95][16] = {\n"
for code in 32...126 {
    let ctx = CGContext(data: nil, width: cellW, height: cellH, bitsPerComponent: 8, bytesPerRow: cellW,
                        space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue)!
    ctx.setFillColor(gray: 0, alpha: 1); ctx.fill(CGRect(x: 0, y: 0, width: cellW, height: cellH))
    ctx.setShouldAntialias(true)
    let text = NSAttributedString(string: String(UnicodeScalar(code)!),
                                  attributes: [kCTFontAttributeName as NSAttributedString.Key: font,
                                               kCTForegroundColorFromContextAttributeName as NSAttributedString.Key: true])
    ctx.setFillColor(gray: 1, alpha: 1)
    ctx.textPosition = CGPoint(x: 0, y: 4)
    CTLineDraw(CTLineCreateWithAttributedString(text), ctx)
    let data = ctx.data!.assumingMemoryBound(to: UInt8.self)
    var rows: [String] = []
    for y in 0..<cellH {
        var byte = 0
        for x in 0..<cellW where data[y * cellW + x] > 90 { byte |= 0x80 >> x }
        rows.append(String(format: "0x%02x", byte))
    }
    let ch = code == 92 ? "backslash" : String(UnicodeScalar(code)!)
    out += "\t{ " + rows.joined(separator: ", ") + " },  // \(ch)\n"
}
out += "};\n"
print(out, terminator: "")
