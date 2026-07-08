// Print the id of the largest on-screen window whose owner name contains argv[1],
// case-insensitive. No match prints nothing. CGWindowList only, no Accessibility.
// Built on demand by the screenshot-macos wrapper.
import CoreGraphics
import Foundation

let needle = (CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "").lowercased()
let info = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements],
                                      kCGNullWindowID) as? [[String: Any]] ?? []
var bestId: Int = -1
var bestArea = -1.0
for w in info {
    guard let owner = (w[kCGWindowOwnerName as String] as? String)?.lowercased(),
          needle.isEmpty || owner.contains(needle),
          let num = w[kCGWindowNumber as String] as? Int,
          let b = w[kCGWindowBounds as String] as? [String: Any],
          let wd = b["Width"] as? Double, let ht = b["Height"] as? Double
    else { continue }
    let area = wd * ht
    if area > bestArea { bestArea = area; bestId = num }
}
if bestId >= 0 { print(bestId) }
