# Publish GlassDock to GitHub via the Git Data API (no git binary required).
# Reads the stored github.com credential from ~/.git-credentials (never prints it).
$ErrorActionPreference = 'Stop'
$root = 'D:\Doubao\GlassDock'
$repo = 'yuzhoudahunzi-beep/GlassDock'
$api  = "https://api.github.com/repos/$repo"

$line = (Get-Content "$env:USERPROFILE\.git-credentials" | Where-Object { $_ -match '@github\.com' } | Select-Object -First 1)
$tok = ($line -replace '^https://','' -replace '@github\.com$','')
if ($tok -match ':') { $tok = ($tok -split ':')[1] }
$h = @{ Authorization = "token $tok"; 'User-Agent' = 'glassdock-publish'; Accept = 'application/vnd.github+json' }

$ref = Invoke-RestMethod -Uri "$api/git/ref/heads/main" -Headers $h
$baseCommit = $ref.object.sha
$commit = Invoke-RestMethod -Uri "$api/git/commits/$baseCommit" -Headers $h
$baseTree = $commit.tree.sha
"base commit $baseCommit"

$files = @(
  'README.md', '.gitignore',
  'src/GlassDock.cpp', 'src/crisptext.h',
  'docs/GLASSDOCK-FULL-MANUAL.md', 'docs/PROJECT_GUIDE.md', 'docs/P7_CHANGELOG.md',
  'docs/P7_TEST_REPORT.md', 'docs/P7_IMPLEMENTATION_PLAN.md',
  'docs/DESKTOP-EMBED-ASSESSMENT-2026-11-24.md', 'docs/PLAN-2026-11-24.md',
  'docs/screenshot.png', 'docs/settings.png',
  'docs/screenshots/hero.png', 'docs/screenshots/widget.png',
  'docs/screenshots/settings.png', 'docs/screenshots/header.png',
  'release/GlassDock.exe', 'release/GlassDock-Setup-1.0.0.exe'
)

$entries = @()
foreach ($f in $files) {
  $p = Join-Path $root ($f -replace '/','\')
  if (-not (Test-Path -LiteralPath $p)) { throw "missing $f" }
  $bytes = [System.IO.File]::ReadAllBytes($p)
  $b64 = [Convert]::ToBase64String($bytes)
  $body = @{ content = $b64; encoding = 'base64' } | ConvertTo-Json -Depth 5
  $blob = Invoke-RestMethod -Method Post -Uri "$api/git/blobs" -Headers $h -Body $body -ContentType 'application/json'
  $entries += @{ path = $f; mode = '100644'; type = 'blob'; sha = $blob.sha }
  "blob $f ($($bytes.Length) B)"
}

$treeBody = @{ base_tree = $baseTree; tree = $entries } | ConvertTo-Json -Depth 6
$tree = Invoke-RestMethod -Method Post -Uri "$api/git/trees" -Headers $h -Body $treeBody -ContentType 'application/json'
"tree $($tree.sha)"

$msg = @"
GlassDock v1.1.0 — living glass, live refresh, bilingual docs

- True per-pixel-alpha see-through glass over static & live wallpapers
- Desktop residency: Win+D state machine, Explorer-restart recovery, keep-on-screen
- Live folder watch (ReadDirectoryChangesW, 120 ms debounce, preserves expansion/scroll)
- System icon font with 3x supersampled grayscale AA (silky edges, no ClearType fringe)
- Header simplified: settings gear only, aligned with the folder name; no count chip
- Settings: fixed 60 Hz drag repaint (picker removed, space left blank), no footer hints
- Bilingual (CN/EN) README rewritten; true-alpha screenshots composited over wallpaper
- Docs brought up to date (full manual / project guide / changelog P8-P11)
"@
$commitBody = @{ message = $msg; tree = $tree.sha; parents = @($baseCommit) } | ConvertTo-Json -Depth 6
$new = Invoke-RestMethod -Method Post -Uri "$api/git/commits" -Headers $h -Body $commitBody -ContentType 'application/json'
"commit $($new.sha)"

$refBody = @{ sha = $new.sha; force = $false } | ConvertTo-Json
$upd = Invoke-RestMethod -Method Patch -Uri "$api/git/refs/heads/main" -Headers $h -Body $refBody -ContentType 'application/json'
"main -> $($upd.object.sha)"

# tag + release with binaries
$tagBody = @{ tag = 'v1.1.0'; object = $new.sha; type = 'commit'; message = 'GlassDock v1.1.0'; tagger = @{ name = 'yuzhoudahunzi-beep'; email = 'yuzhoudahunzi@gmail.com'; date = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') } } | ConvertTo-Json -Depth 6
$tag = Invoke-RestMethod -Method Post -Uri "$api/git/tags" -Headers $h -Body $tagBody -ContentType 'application/json'
$tagRefBody = @{ ref = 'refs/tags/v1.1.0'; sha = $tag.sha } | ConvertTo-Json
[void](Invoke-RestMethod -Method Post -Uri "$api/git/refs" -Headers $h -Body $tagRefBody -ContentType 'application/json')
"tag v1.1.0"

$relBody = @{
  tag_name = 'v1.1.0'; name = 'GlassDock v1.1.0 — 活玻璃 / Living Glass'
  body = @"
**中文**：真透明桌面文件夹小组件。本轮带来实时目录监听、系统字体 3x 超采样平滑渲染、精简头部（仅齿轮）、固定 60Hz、中英双语 README 与真 alpha 截图。
**English**: true see-through desktop folder widget. This release adds live directory watching, 3x-supersampled system-font rendering, a simplified header (gear only), fixed 60 Hz repaint, a bilingual README and true-alpha screenshots.

- ``GlassDock-Setup-1.0.0.exe`` — installer (shortcuts + uninstall entry)
- ``GlassDock.exe`` — portable, zero dependencies
"@
  draft = $false; prerelease = $false
} | ConvertTo-Json -Depth 6
$rel = Invoke-RestMethod -Method Post -Uri "$api/releases" -Headers $h -Body $relBody -ContentType 'application/json'
"release id $($rel.id)"

$upH = @{ Authorization = "token $tok"; 'User-Agent' = 'glassdock-publish'; 'Content-Type' = 'application/octet-stream' }
foreach ($a in @('GlassDock-Setup-1.0.0.exe','GlassDock.exe')) {
  $p = Join-Path $root "release\$a"
  $bytes = [System.IO.File]::ReadAllBytes($p)
  $u = "https://uploads.github.com/repos/$repo/releases/$($rel.id)/assets?name=$a"
  $ast = Invoke-RestMethod -Method Post -Uri $u -Headers $upH -Body $bytes
  "asset $a -> $($ast.browser_download_url)"
}
"DONE https://github.com/$repo"