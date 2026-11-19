$ErrorActionPreference = 'Stop'
$api = 'https://artwork.m8tec.top'
$album = 'The Little Mermaid (Original Motion Picture Soundtrack)'
$artists = @('Halle','Alan Menken & Howard Ashman','Alan Menken','Howard Ashman','Various Artists')
foreach ($artist in $artists) {
  $url = "$api/api/v1/artwork/search?artist=$([uri]::EscapeDataString($artist))&album=$([uri]::EscapeDataString($album))"
  try {
    $response = Invoke-WebRequest -Uri $url -Method Get -SkipHttpErrorCheck -TimeoutSec 12
    $obj = if ($response.StatusCode -eq 200) { $response.Content | ConvertFrom-Json } else { $null }
    Write-Host "Mermaid search: artist=$artist HTTP=$($response.StatusCode) release=$($obj.artist) / $($obj.album) motion=$([bool]($obj.url -or $obj.url_tall))"
  } catch {
    Write-Warning "Mermaid search failure for $artist : $($_.Exception.Message)"
  }
  Start-Sleep -Milliseconds 450
}
# The exact public official Apple Music album, not a guessed ID:
$appleAlbum = 'https://music.apple.com/us/album/the-little-mermaid-original-motion-picture-soundtrack/1684317223'
$uri = "$api/api/v1/artwork/url?url=$([uri]::EscapeDataString($appleAlbum))"
try {
  $response = Invoke-WebRequest -Uri $uri -Method Get -SkipHttpErrorCheck -TimeoutSec 15
  $obj = if ($response.StatusCode -eq 200) { $response.Content | ConvertFrom-Json } else { $null }
  Write-Host "Mermaid direct official album: HTTP=$($response.StatusCode), artist=$($obj.artist), album=$($obj.album), square=$([bool]$obj.url), tall=$([bool]$obj.url_tall)"
} catch {
  Write-Warning "Mermaid direct URL lookup failure: $($_.Exception.Message)"
}
$term=[uri]::EscapeDataString($album)
try {
  $itunes=Invoke-RestMethod -Uri "https://itunes.apple.com/search?term=$term&entity=album&country=US&limit=30" -TimeoutSec 15
  @($itunes.results | Where-Object { $_.collectionName -like '*Little Mermaid*' } | Select-Object -First 10) | ForEach-Object {
    Write-Host "iTunes album-only: id=$($_.collectionId), artist=$($_.artistName), album=$($_.collectionName)"
  }
  $track=Invoke-RestMethod -Uri 'https://itunes.apple.com/lookup?id=1684317223&entity=song&limit=200' -TimeoutSec 15
  @($track.results | Where-Object { $_.trackName -like '*Part of Your World*' } | Select-Object -First 10) | ForEach-Object {
    Write-Host "iTunes track proof: artist=$($_.artistName) track=$($_.trackName) album=$($_.collectionName) album_id=$($_.collectionId)"
  }
} catch {
  Write-Warning "iTunes lookup failure: $($_.Exception.Message)"
}

# Inspect source motion FPS without downloading clips locally.
try {
  $look = Invoke-RestMethod -Uri "$api/api/v1/artwork/search?artist=Katy%20Perry&album=143" -TimeoutSec 15
  if ($look.url) {
    $master = (Invoke-WebRequest -Uri $look.url -TimeoutSec 15).Content
    if ($master -is [byte[]]) { $master = [Text.Encoding]::UTF8.GetString($master) }
    $master -split "\n" | Where-Object { $_ -like '#EXT-X-STREAM-INF:*' } | Select-Object -First 20 | ForEach-Object {
      if ($_ -match 'FRAME-RATE=([^,]+)') { $fr = $Matches[1] } else { $fr = 'unspecified' }
      if ($_ -match 'RESOLUTION=([0-9]+x[0-9]+)') { $res = $Matches[1] } else { $res = 'unknown' }
      if ($_ -match 'CODECS="([^"]+)"') { $codec = $Matches[1] } else { $codec = 'unknown' }
      Write-Host "Katy source FPS: $res $codec $fr"
    }
  }
} catch { Write-Warning "Katy source FPS probe failed: $($_.Exception.Message)" }
