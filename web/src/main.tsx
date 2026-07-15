import React from "react";
import { createRoot } from "react-dom/client";
import { App } from "./App.tsx";
import "./i18n.ts";
import "./app.css";

const rootElement = document.getElementById("root");
if (!rootElement) {
  throw new Error("Root element #root not found");
}

createRoot(rootElement).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
