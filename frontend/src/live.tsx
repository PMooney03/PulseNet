import { createContext, ReactNode, useCallback, useContext, useEffect, useState } from "react";
import { api, Incident, Summary, Target } from "./model";

interface LiveData {
  targets: Target[];
  summary: Summary | null;
  incidents: Incident[];
  loaded: boolean;
  error: string;
  live: boolean;
  setError: (message: string) => void;
  reload: () => Promise<void>;
}

const LiveContext = createContext<LiveData | null>(null);

export function LiveProvider({ children }: { children: ReactNode }) {
  const [targets, setTargets] = useState<Target[]>([]);
  const [summary, setSummary] = useState<Summary | null>(null);
  const [incidents, setIncidents] = useState<Incident[]>([]);
  const [loaded, setLoaded] = useState(false);
  const [error, setError] = useState("");
  const [live, setLive] = useState(false);

  const reload = useCallback(async () => {
    const [listed, open] = await Promise.all([
      api<{ targets: Target[]; summary: Summary }>("/api/targets"),
      api<{ incidents: Incident[] }>("/api/incidents"),
    ]);
    setTargets(listed.targets);
    setSummary(listed.summary);
    setIncidents(open.incidents);
    setLoaded(true);
  }, []);

  useEffect(() => {
    reload().catch((reason: Error) => {
      setLoaded(true);
      setError(reason.message);
    });
  }, [reload]);

  useEffect(() => {
    let socket: WebSocket | null = null;
    let stopped = false;
    let timer = 0;

    const connect = () => {
      const protocol = location.protocol === "https:" ? "wss" : "ws";
      socket = new WebSocket(`${protocol}://${location.host}/api/live`);
      socket.onopen = () => setLive(true);
      socket.onclose = () => {
        setLive(false);
        if (!stopped) timer = window.setTimeout(connect, 2000);
      };
      socket.onmessage = () => {
        reload().catch((reason: Error) => setError(reason.message));
      };
    };
    connect();
    return () => {
      stopped = true;
      window.clearTimeout(timer);
      socket?.close();
    };
  }, [reload]);

  return (
    <LiveContext.Provider value={{ targets, summary, incidents, loaded, error, live, setError, reload }}>
      {children}
    </LiveContext.Provider>
  );
}

export function useLive(): LiveData {
  const data = useContext(LiveContext);
  if (!data) throw new Error("Live data is only available inside the dashboard");
  return data;
}
