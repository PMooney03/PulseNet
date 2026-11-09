import { ReactNode, useEffect, useState } from "react";
import { navigate } from "./model";

export function useLocation(): string {
  const [href, setHref] = useState(() => window.location.pathname + window.location.search);
  useEffect(() => {
    const sync = () => setHref(window.location.pathname + window.location.search);
    window.addEventListener("popstate", sync);
    return () => window.removeEventListener("popstate", sync);
  }, []);
  return href;
}

export function Link({
  to,
  className,
  children,
}: {
  to: string;
  className?: string;
  children: ReactNode;
}) {
  return (
    <a
      className={className}
      href={to}
      onClick={(event) => {
        if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey || event.button !== 0) return;
        event.preventDefault();
        navigate(to);
      }}
    >
      {children}
    </a>
  );
}

export function targetIdFromPath(path: string): number | null {
  const match = path.match(/^\/targets\/(\d+)\/?$/);
  if (!match) return null;
  return Number(match[1]);
}
