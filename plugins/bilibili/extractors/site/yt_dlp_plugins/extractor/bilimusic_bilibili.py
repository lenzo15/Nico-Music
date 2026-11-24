"""NicoMusic source plugin: resolve a Bilibili page to an audio-first media result."""
from urllib.parse import urlsplit, urlunsplit

from yt_dlp.extractor.common import InfoExtractor
from yt_dlp.utils import ExtractorError


class BiliMusicBilibiliIE(InfoExtractor):
    IE_NAME = 'bilimusic:bilibili'
    _VALID_URL = r'bm-bilibili://(?P<id>https?://.+)'

    def _real_extract(self, url):
        page = self._match_id(url)
        parts = urlsplit(page)
        if (parts.hostname or '').rstrip('.') in ('b23.tv', 'bili2233.cn'):
            page = self._request_webpage(page, None, note='Resolving Bilibili share link').url
            parts = urlsplit(page)
        host = (parts.hostname or '').lower().rstrip('.')
        if host != 'bilibili.com' and not host.endswith('.bilibili.com'):
            raise ExtractorError('Not a Bilibili page', expected=True)
        if host in ('m.bilibili.com', 'bilibili.com'):
            page = urlunsplit(('https', 'www.bilibili.com', parts.path, parts.query, ''))

        # Reuse yt-dlp's maintained site client (WBI, part selection, formats and
        # playback headers). Keep signed CDN URLs transient: resolve again on play.
        video_ie = self._downloader.get_info_extractor('BiliBili')
        if not video_ie.suitable(page):
            return self.url_result(page)  # audio pages, live rooms, bangumi, etc.
        result = video_ie.extract(page)
        if result.get('_type', 'video') == 'video':
            audio = [f for f in result.get('formats', ()) if f.get('vcodec') == 'none']
            if audio:
                result['formats'] = audio
            result['webpage_url'] = page
            thumb = result.get('thumbnail') or ''
            if thumb.startswith('http://'):
                result['thumbnail'] = 'https://' + thumb[7:]
        return result
