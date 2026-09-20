import React, {useId, useRef, useState} from 'react';

export default function AdmonitionLayout({type, title, icon, children, className}) {
  const [open, setOpen] = useState(true);
  const id = useId();
  const toggle = useRef(null);
  const collapse = () => {
    setOpen(false);
    toggle.current?.focus();
  };

  return (
    <div className={`theme-admonition theme-admonition-${type} ${className || ''}`}>
      <div className="admonitionHeading">
        <span id={id + '-title'} className="xp-dialog-title">{title}</span>
        <div className="xp-window-controls">
          <button ref={toggle} type="button" aria-label={open ? 'Minimize message' : 'Restore message'}
            aria-expanded={open} aria-controls={id} onClick={() => setOpen(!open)}>
            <span aria-hidden="true">{open ? '−' : '□'}</span>
          </button>
          <button type="button" className="xp-close" aria-label="Close message" onClick={collapse} disabled={!open}>
            <span aria-hidden="true">×</span>
          </button>
        </div>
      </div>
      <div id={id} className="admonitionContent" hidden={!open} aria-labelledby={id + '-title'}>
        {icon && <span className="admonitionIcon" aria-hidden="true">{icon}</span>}
        <div className="xp-message">{children}</div>
        <div className="xp-dialog-actions">
          <button type="button" className="xp-button" onClick={collapse}>OK</button>
        </div>
      </div>
      {!open && <div className="xp-dialog-restore">
        <button type="button" className="xp-button" onClick={() => setOpen(true)} aria-controls={id}>Show message</button>
      </div>}
    </div>
  );
}
