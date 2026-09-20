import React, {useEffect, useState} from 'react';
import {useLocation} from '@docusaurus/router';
import Layout from '@theme-original/DocItem/Layout';

function ReadingProgress() {
  const [progress, setProgress] = useState(0);
  useEffect(() => {
    let frame;
    const update = () => {
      cancelAnimationFrame(frame);
      frame = requestAnimationFrame(() => {
        const range = document.documentElement.scrollHeight - window.innerHeight;
        setProgress(range <= 0 ? 100 : Math.round(Math.min(1, Math.max(0, window.scrollY / range)) * 100));
      });
    };
    // Images and collapsed callouts can change the page height after loading.
    const observer = new ResizeObserver(update);
    observer.observe(document.body);
    window.addEventListener('scroll', update, {passive: true});
    window.addEventListener('resize', update);
    update();
    return () => {
      cancelAnimationFrame(frame);
      observer.disconnect();
      window.removeEventListener('scroll', update);
      window.removeEventListener('resize', update);
    };
  }, []);

  return (
    <div className="xp-reading">
      <span>Reading progress</span>
      <progress className="xp-progress" aria-label="Reading progress" max="100" value={progress} />
      <span className="xp-progress-value" aria-hidden="true">{progress}%</span>
    </div>
  );
}

export default function DocLayout(props) {
  const {pathname} = useLocation();
  return <div className="xp-doc-page">
    <Layout {...props} />
    <ReadingProgress key={pathname} />
  </div>;
}
