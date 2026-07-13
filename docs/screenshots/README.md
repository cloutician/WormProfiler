# Screenshot Guide

Use this folder for screenshots that are referenced by the GitHub README. Keep filenames stable so README links do not break when screenshots are updated.

## Current README files

| File | What to capture |
| --- | --- |
| `main-window.png` | Full WormProfiler window with an original image and processed preview visible. |
| `side-by-side-workspace.png` | Original and processed previews shown together. |
| `preprocessing-controls.png` | The preprocessing controls area with representative settings enabled. |
| `clahe-example.png` | Before-and-after example for grayscale or CLAHE enhancement. |
| `auto-crop-result.gif` | Animated classical auto-crop result with the worm framed tightly. |
| `apply-crop-model.png` | ONNX crop model selection or crop-model workflow entry point. |
| `exported-crops.png` | A folder or collage showing exported crop PNGs. |

## Optional future screenshots

| File | What to capture |
| --- | --- |
| `crop-review-dialog.png` | The crop review dialog showing an editable crop rectangle. |
| `mask-overlay.png` | A loaded mask overlay displayed against the original image. |
| `batch-preprocessing.png` | Batch preprocessing dialog, progress, or completion state. |
| `workflow-collage.png` | Optional overview image combining raw input, processed preview, crop review, and exported crops. |

## Capture checklist

- Use anonymized, synthetic, or approved sample microscopy data.
- Capture at a readable desktop size, ideally around 1200 to 1600 px wide.
- Crop out unrelated desktop chrome, notifications, file paths, and personal folders.
- Use PNG for static UI screenshots. Use GIF for short workflow animations, such as `auto-crop-result.gif`.
- Compress large images before committing; aim for under 1 MB per image when practical.
- Use lowercase kebab-case filenames.

## README embed snippets

Single hero screenshot:

```html
<p align="center">
  <img src="docs/screenshots/main-window.png" alt="WormProfiler main window showing original and processed worm microscopy previews" width="900">
</p>
```

Two-column feature screenshots:

```html
<table>
  <tr>
    <td align="center">
      <img src="docs/screenshots/preprocessing-controls.png" alt="WormProfiler preprocessing controls" width="420"><br>
      <strong>Preprocessing controls</strong>
    </td>
    <td align="center">
      <img src="docs/screenshots/apply-crop-model.png" alt="WormProfiler crop model workflow" width="420"><br>
      <strong>Crop model workflow</strong>
    </td>
  </tr>
</table>
```

Animated auto-crop result:

```html
<p align="center">
  <img src="docs/screenshots/auto-crop-result.gif" alt="Animated WormProfiler auto-crop result" width="900">
</p>
```
