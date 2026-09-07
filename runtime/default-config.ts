import { defineConfig, type Config, type SeverityProfile } from "./config";

const local: SeverityProfile = { default: "warning" };
const ci: SeverityProfile = { default: "error" };
const profiles: Config["profiles"] = { local, ci };

export const defaultConfig: Config = defineConfig({ profiles });
