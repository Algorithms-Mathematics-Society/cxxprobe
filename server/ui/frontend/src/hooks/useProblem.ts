import { useQuery } from "@tanstack/react-query";
import { fetchProblem } from "../api/client";

export function useProblem(slug: string | undefined) {
  return useQuery({
    queryKey: ["problem", slug],
    queryFn: () => fetchProblem(slug as string),
    enabled: Boolean(slug),
  });
}
