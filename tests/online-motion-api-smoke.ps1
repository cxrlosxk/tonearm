# Probe independent public motion-artwork lookup and genuine Apple CDN HLS.
# No private Apple API credentials are fetched or used.
$ErrorActionPreference = 'Stop'
$queries = @(
    @{ artist = 'Gorillaz'; album = 'Plastic Beach'; title = '' },
    @{ artist = 'Kelly Clarkson'; album = 'Wrapped in Red'; title = '' },
    @{ artist = 'Cynthia Erivo'; album = 'Wicked: For Good - The Soundtrack'; title = 'No Good Deed' },
    @{ artist = 'Wicked Movie Cast'; album = 'Wicked: For Good - The Soundtrack'; title = 'No Good Deed' }
)
$found = $false
$cachePassed = $false
foreach ($q in $queries) {
    $artist = [uri]::EscapeDataString($q.artist)
    $album = [uri]::EscapeDataString($q.album)
    $title = [uri]::EscapeDataString($q.title)
    $endpoint = "https://artwork.m8tec.top/api/v1/artwork/search?artist=$artist&album=$album&title=$title"
    try {
        $response = Invoke-RestMethod -Uri $endpoint -Method Get -TimeoutSec 20 -Headers @{Accept='application/json'}
        $hls = if ($response.url -match '^https://') { $response.url } else { $response.url_tall }
        if ($hls -match '^https://') {
            $streamResponse = Invoke-WebRequest -Uri $hls -Method Get -TimeoutSec 20
            $raw = $streamResponse.Content
            $manifest = if ($raw -is [byte[]]) { [Text.Encoding]::UTF8.GetString($raw) } else { [string]$raw }
            Write-Host "HLS primary playlist diagnostics:"
            $manifest -split "`n" | Select-Object -First 18 | ForEach-Object {
                $ln = $_.Trim()
                if ($ln.StartsWith("#")) { Write-Host $ln }
                elseif ($ln) {
                    $p = [uri]::new([uri]$hls, $ln)
                    Write-Host "URI host=$($p.Host) suffix=$([IO.Path]::GetExtension($p.AbsolutePath))"
                }
            }
            if ($q.artist -eq 'Gorillaz') {
                $masterRows = @($manifest -split "`n")
                for ($i = 0; $i -lt $masterRows.Count - 1; $i++) {
                    if ($masterRows[$i] -like '#EXT-X-STREAM-INF:*' -and $masterRows[$i] -match 'avc1') {
                        $variant = [uri]::new([uri]$hls, $masterRows[$i+1].Trim())
                        $vResponse = Invoke-WebRequest -Uri $variant.AbsoluteUri -Method Get -TimeoutSec 20
                        $vContent = $vResponse.Content
                        $vText = if ($vContent -is [byte[]]) { [Text.Encoding]::UTF8.GetString($vContent) } else { [string]$vContent }
                        Write-Host "HLS media playlist diagnostic ($($variant.Host)):"
                        $vText -split "`n" | Select-Object -First 35 | ForEach-Object {
                            $ln = $_.Trim()
                            if ($ln.StartsWith("#")) { Write-Host ($ln -replace 'URI="[^"]+"','URI="[media-url]"') }
                            elseif ($ln) {
                                $segmentUri = [uri]::new($variant,$ln)
                                Write-Host "MEDIA host=$($segmentUri.Host) suffix=$([IO.Path]::GetExtension($segmentUri.AbsolutePath))"
                            }
                        }
                        break
                    }
                }
            }
            if ($manifest -notmatch '#EXTM3U') {
                throw 'URL returned non-HLS content'
            }
            $found = $true
            if ($q.artist -eq 'Gorillaz') {
                $cacheProbe = Join-Path (Get-Location) "build/RelWithDebInfo/tonearm-hls-cache-smoke.exe"
                if (-not (Test-Path $cacheProbe)) { throw "Native HLS cache probe executable was not built" }
                $env:TONEARM_HLS_DIAGNOSTICS = '1'
                & $cacheProbe $hls
                if ($LASTEXITCODE -ne 0) { throw "Native offline HLS cache validation failed" }
                $cachePassed = $true
                Write-Host "Native cached HLS validation succeeded"
            }
            Write-Host "Online motion HLS: requested=$($q.artist) / $($q.album), response=$($response.artist) / $($response.album), CDN=$(([uri]$hls).Host)"
            # Check that the playlist also contains moving frames. No artwork is saved in the repo.
            $ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
            if ($ffmpeg -and $q.artist -eq 'Gorillaz') {
                $outputDir = Join-Path $env:RUNNER_TEMP "tonearm-hls-probe"
                New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
                & $ffmpeg.Source -hide_banner -loglevel error -y -i $hls -vf "fps=1" -frames:v 4 (Join-Path $outputDir "frame-%02d.png") 2>&1 | ForEach-Object { Write-Host $_ }
                if ($LASTEXITCODE -eq 0) {
                    $files = @(Get-ChildItem $outputDir -Filter "frame-*.png" | Sort-Object Name)
                    $hashes = @($files | ForEach-Object {(Get-FileHash $_.FullName -Algorithm SHA256).Hash} | Select-Object -Unique)
                    Write-Host "Decoded motion frames: count=$($files.Count), unique SHA256=$($hashes.Count)"
                } else {
                    Write-Warning "FFmpeg couldn't decode sampled remote HLS frames."
                }
            }
        } else {
            Write-Host "No animated artwork URL for requested=$($q.artist) / $($q.album)"
        }
    } catch {
        Write-Warning "Lookup unavailable for $($q.artist) / $($q.album): $($_.Exception.Message)"
    }
    Start-Sleep -Milliseconds 750
}
if (-not $found) { throw 'No tested album returned a readable online HLS playlist.' }
if (-not $cachePassed) { throw 'Native Windows HLS cache probe did not succeed.' }
