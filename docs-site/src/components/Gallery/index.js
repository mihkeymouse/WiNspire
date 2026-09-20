import React, {useEffect, useState} from 'react';
import gallery from '@site/src/data/gallery.json';

export default function Gallery() {
  const items = gallery.groups.flatMap((group) =>
    gallery.items.filter((item) => item.group === group.id),
  );
  const [activeIndex, setActiveIndex] = useState(null);
  const activeItem = activeIndex === null ? null : items[activeIndex];

  function show(offset) {
    setActiveIndex((index) => (index + offset + items.length) % items.length);
  }

  useEffect(() => {
    if (!activeItem) return undefined;
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';
    const onKeyDown = (event) => {
      if (event.key === 'Escape') setActiveIndex(null);
      if (event.key === 'ArrowLeft') show(-1);
      if (event.key === 'ArrowRight') show(1);
    };
    window.addEventListener('keydown', onKeyDown);
    return () => {
      document.body.style.overflow = previousOverflow;
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [activeItem]);

  return (
    <>
      <div className="winspire-gallery-sections">
        {gallery.groups.map((group) => {
          const groupItems = gallery.items.filter((item) => item.group === group.id);
          if (!groupItems.length) return null;
          return <section className="winspire-gallery-section" key={group.id}>
            <h3>{group.label}</h3>
            <div className="winspire-gallery">
              {groupItems.map((item) => <button
                className="winspire-gallery-thumb"
                key={item.id}
                type="button"
                onClick={() => setActiveIndex(items.findIndex((entry) => entry.id === item.id))}>
                <figure>
                  <img src={item.url} alt={item.alt} loading="lazy"/>
                </figure>
              </button>)}
            </div>
          </section>;
        })}
      </div>
      {activeItem && <div
        className="winspire-lightbox"
        role="dialog"
        aria-modal="true"
        aria-label="WiNspire gallery image viewer"
        onMouseDown={(event) => {
          if (event.target === event.currentTarget) setActiveIndex(null);
        }}>
        <div className="winspire-lightbox-window">
          <div className="winspire-lightbox-title">
            <span>WiNspire Gallery</span>
            <button type="button" aria-label="Close image viewer" onClick={() => setActiveIndex(null)}>×</button>
          </div>
          <div className="winspire-lightbox-body">
            {items.length > 1 && <button className="winspire-lightbox-nav winspire-lightbox-prev" type="button" aria-label="Previous picture" onClick={() => show(-1)}>‹</button>}
            <img src={activeItem.url} alt={activeItem.alt}/>
            {items.length > 1 && <button className="winspire-lightbox-nav winspire-lightbox-next" type="button" aria-label="Next picture" onClick={() => show(1)}>›</button>}
          </div>
          <div className="winspire-lightbox-caption">
            <span>{activeIndex + 1} / {items.length}</span>
          </div>
        </div>
      </div>}
    </>
  );
}
