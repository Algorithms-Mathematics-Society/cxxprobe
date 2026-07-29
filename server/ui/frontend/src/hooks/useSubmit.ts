import { useMutation, useQueryClient } from "@tanstack/react-query";
import { submitSolution, type SubmitPayload } from "../api/client";

export function useSubmit() {
  const queryClient = useQueryClient();
  return useMutation({
    mutationFn: (payload: SubmitPayload) => submitSolution(payload),
    onSuccess: () => {
      // A new row exists now — history will pick it up next time it's viewed.
      queryClient.invalidateQueries({ queryKey: ["history"] });
    },
  });
}
