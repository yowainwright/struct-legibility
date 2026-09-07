import { defineConfig, start, type Rule, type SeverityProfile } from "../../../runtime/index";

const local: SeverityProfile = { default: "warning" };
const profiles = { local };

const dynamicRule: Rule = () => {
  eval("1 + 1");
  return [];
};

const rules = [dynamicRule];
const config = defineConfig({ profiles, rules });

start(config);
