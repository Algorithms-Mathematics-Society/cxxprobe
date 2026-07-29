import { useQuery } from "@tanstack/react-query";
import { fetchHistory } from "../api/client";

export function useHistory(limit = 25) {
  return useQuery({
    queryKey: ["history", limit],
    queryFn: () => fetchHistory(limit),
    select: (data) => data.submissions,
  });
}
