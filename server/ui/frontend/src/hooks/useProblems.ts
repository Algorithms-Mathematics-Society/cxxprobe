import { useQuery } from "@tanstack/react-query";
import { fetchProblems } from "../api/client";

export function useProblems() {
  return useQuery({
    queryKey: ["problems"],
    queryFn: fetchProblems,
    select: (data) => data.problems,
  });
}
