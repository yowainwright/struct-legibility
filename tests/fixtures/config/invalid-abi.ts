// @tqs-script
import "../../../runtime/native";

type Probe = () => unknown;

const rejects = (probe: Probe): boolean => {
  try {
    probe();
    return false;
  } catch {
    return true;
  }
};

const pathObject = { toString: (): string => scriptArgs[1] ?? "." };
const textObject = { toString: (): string => "" };
const invalidPath = (): unknown => __slAnalyze([pathObject as unknown as string], true, false);
const invalidIgnore = (): unknown => __slAnalyze(scriptArgs.slice(1), 1 as unknown as boolean, false);
const invalidFacts = (): unknown => __slAnalyze(scriptArgs.slice(1), true, 0 as unknown as boolean);
const invalidWrite = (): void => __slWrite(textObject as unknown as string, false);
const invalidExit = (): void => __slSetExitCode(1.5);
const probes = [invalidPath, invalidIgnore, invalidFacts, invalidWrite, invalidExit];
const allRejected = probes.every(rejects);

__slSetExitCode(allRejected ? 0 : 1);
