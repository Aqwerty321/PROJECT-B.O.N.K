import { useEffect, useState } from 'react';
import { DEFAULT_PAGE_ID, hashForPage, pageIdFromHash, type PageId } from './navigation';

function readPageId(): PageId {
  if (typeof window === 'undefined') {
    return DEFAULT_PAGE_ID;
  }

  const pageId = pageIdFromHash(window.location.hash);
  if (!window.location.hash) {
    window.history.replaceState(null, '', hashForPage(pageId));
  }
  return pageId;
}

export function useHashRoute() {
  const [pageId, setPageId] = useState<PageId>(() => readPageId());

  useEffect(() => {
    const onHashChange = () => setPageId(readPageId());
    window.addEventListener('hashchange', onHashChange);
    onHashChange();
    return () => window.removeEventListener('hashchange', onHashChange);
  }, []);

  const navigate = (nextPage: PageId) => {
    const nextHash = hashForPage(nextPage);
    if (window.location.hash === nextHash) {
      setPageId(nextPage);
      return;
    }
    window.location.hash = nextHash;
  };

  return { pageId, navigate };
}
