$ErrorActionPreference = 'Stop'
$magick = (Get-Command magick -ErrorAction Stop).Source
$records = @()
foreach ($profile in @(@{ Name='x4pro'; Width=480; Height=800 }, @{ Name='x3'; Width=528; Height=792 })) {
    $destination = Join-Path $PSScriptRoot ('generated/' + $profile.Name + '/Pictures/CrossLoop')
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach ($name in @('Forest-Dawn', 'Quiet-Orbit')) {
        $source = Join-Path $PSScriptRoot ('source/' + $name + '.png')
        $target = Join-Path $destination ($name + '.bmp')
        $dimensions = "$($profile.Width)x$($profile.Height)"
        & $magick $source -resize ($dimensions + '^') -gravity center -extent $dimensions -alpha off -colorspace gray -threshold '50%' -type bilevel -compress none ('BMP3:' + $target)
        if ($LASTEXITCODE -ne 0) { throw "Image conversion failed for $name on $($profile.Name)" }
        $bytes = [System.IO.File]::ReadAllBytes($target)
        if ([System.BitConverter]::ToInt32($bytes,18) -ne $profile.Width -or [Math]::Abs([System.BitConverter]::ToInt32($bytes,22)) -ne $profile.Height -or [System.BitConverter]::ToUInt16($bytes,28) -ne 1 -or [System.BitConverter]::ToUInt32($bytes,30) -ne 0) {
            throw "Unexpected bitmap format: $target"
        }
        $records += @{ device=$profile.Name; name=$name; width=$profile.Width; height=$profile.Height; bpp=1; bytes=$bytes.Length; sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    Compress-Archive -LiteralPath (Join-Path $PSScriptRoot ('generated/' + $profile.Name + '/Pictures')) -DestinationPath (Join-Path $PSScriptRoot ('generated/CrossLoop-Pictures-' + $profile.Name + '.zip')) -Force
}
$records | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'generated/manifest.json') -Encoding utf8
Write-Output 'Prepared two pictures for each device; headers and dimensions verified.'
