# Diagnostic smoke test of the independent public motion-artwork API.
# Does not exercise Apple's private API or authenticate to Apple.
$ErrorActionPreference = 'Stop'
$queries = @(
    @{ artist = 'Gorillaz'; album = 'Plastic Beach' },
    @{ artist = 'Linkin Park'; album = 'Living Things' },
    @{ artist = 'Beyonce'; album = 'RENAISSANCE' }
)
foreach ($q in $queries) {
    $artist = [uri]::EscapeDataString($q.artist)
    $album = [uri]::EscapeDataString($q.album)
    $endpoint = "https://artwork.m8tec.top/api/v1/artwork/search?artist=$artist&album=$album"
    try {
        $response = Invoke-RestMethod -Uri $endpoint -Method Get -TimeoutSec 20 -Headers @{Accept='application/json'}
        $hls = if ($response.url -match '^https://') { $response.url } else { $response.url_tall }
        if ($hls -match '^https://') {
            $streamResponse = Invoke-WebRequest -Uri $hls -Method Get -TimeoutSec 20
            $raw = $streamResponse.Content
            $manifest = if ($raw -is [byte[]]) { [Text.Encoding]::UTF8.GetString($raw) } else { [string]$raw }
            $preview = ($manifest.Substring(0, [Math]::Min(100, $manifest.Length)) -replace '[^\x20-\x7E]', '.')
            Write-Host "CDN diagnostic: host=$(([uri]$hls).Host) status=$($streamResponse.StatusCode) content-type=$($streamResponse.Headers['Content-Type']) length=$($manifest.Length) start=$preview"
            if (-not ($manifest -match '#EXTM3U')) {
                throw 'Online artwork URL did not return an HLS manifest'
            }
            Write-Host "Found genuine online HLS artwork for $($q.artist) — $($q.album)"
            Write-Host "Returned release identity: $($response.artist) — $($response.album)"
            Write-Host "HLS playlist successfully downloaded from $(([uri]$hls).Host)"
            exit 0
        }
    } catch {
        Write-Warning "Lookup unavailable for $($q.artist) — $($q.album): $($_.Exception.Message)"
    }
}
throw 'No tested album returned an online HLS artwork URL. Remote lookups must be verified before release.'
