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
        if (($response.url -match '^https://') -or ($response.url_tall -match '^https://')) {
            Write-Host "Found genuine online HLS artwork for $($q.artist) — $($q.album)"
            Write-Host "Returned release identity: $($response.artist) — $($response.album)"
            exit 0
        }
    } catch {
        Write-Warning "Lookup unavailable for $($q.artist) — $($q.album): $($_.Exception.Message)"
    }
}
throw 'No tested album returned an online HLS artwork URL. Remote lookups must be verified before release.'
