"""Example: pages that publish an audio file in an Open Graph audio tag."""
from urllib.parse import urljoin, urlsplit

from yt_dlp.extractor.common import InfoExtractor
from yt_dlp.utils import ExtractorError


class BiliMusicWebRadioIE(InfoExtractor):
    IE_NAME = 'bilimusic:webradio'
    _VALID_URL = r'bm-webradio://(?P<id>https?://.+)'

    def _real_extract(self, url):
        page = self._match_id(url)
        key = urlsplit(page).path.rstrip('/').rsplit('/', 1)[-1] or 'radio'
        html = self._download_webpage(page, key)
        audio = self._html_search_meta(('radio:stream', 'og:audio:secure_url', 'og:audio:url', 'og:audio'), html)
        stream = urljoin(page, audio) if audio else None
        if not stream or urlsplit(stream).scheme not in ('https', 'http'):
            raise ExtractorError('Page has no HTTP(S) audio stream', expected=True)
        cover = self._og_search_thumbnail(html, default=None)
        return {
            'id': key,
            'webpage_url': page,
            'title': self._og_search_title(html, default=None) or self._html_extract_title(html) or key,
            'thumbnail': urljoin(page, cover) if cover else None,
            'url': stream,
            'vcodec': 'none',
            'http_headers': {'Referer': page},
        }
