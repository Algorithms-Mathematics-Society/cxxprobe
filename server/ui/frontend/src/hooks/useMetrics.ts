import { useQuery } from "@tanstack/react-query";
import { fetchMetricsText } from "../api/client";

export function useMetricsText() {
  return useQuery({
    queryKey: ["metrics-text"],
    queryFn: fetchMetricsText,
    refetchInterval: 5000,
  });
}
