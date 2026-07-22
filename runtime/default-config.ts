import { defineConfig, type SeverityProfile } from "./config";

const local: SeverityProfile = { default: "warning" };
const ci: SeverityProfile = { default: "error" };
const profiles = { local, ci };

export const defaultConfig = defineConfig({ profiles });
