// @tqs-script
import * as os from "qjs:os";
import { defineConfig, start, type Rule, type SeverityProfile } from "../../../runtime/index";

const local: SeverityProfile = { default: "warning" };
const profiles = { local };

const operatingSystemRule: Rule = () => {
  os.getcwd();
  return [];
};

const rules = [operatingSystemRule];
const config = defineConfig({ profiles, rules });

start(config);
