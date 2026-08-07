using NHN.InGame;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class InGameManualTuningUtility
{
    [MenuItem("NHN/Prototype/Prepare Open InGame Scene For Manual Tuning")]
    public static void PrepareOpenScene()
    {
        LocalInGameController controller = Object.FindFirstObjectByType<LocalInGameController>();
        if (controller != null)
        {
            controller.applyInGameBoardWorldSize = false;
            controller.syncResultGridFromBoardTarget = true;
            controller.useResultGridOverride = false;
            EditorUtility.SetDirty(controller);
        }

        PaperBoardTarget boardTarget = Object.FindFirstObjectByType<PaperBoardTarget>();
        if (boardTarget != null)
        {
            boardTarget.drawDebugGrid = true;
            EditorUtility.SetDirty(boardTarget);
        }

        PaperBoardGridRenderer gridRenderer = Object.FindFirstObjectByType<PaperBoardGridRenderer>();
        if (gridRenderer != null)
        {
            gridRenderer.syncFromTarget = true;
            gridRenderer.RefreshFromTarget();
            EditorUtility.SetDirty(gridRenderer);
        }

        ScarecrowPaperCarrier carrier = Object.FindFirstObjectByType<ScarecrowPaperCarrier>();
        if (carrier != null)
        {
            carrier.autoLayoutScarecrows = false;
            carrier.useScarecrowAnchors = true;
            carrier.EnsureScarecrowAnchors();
            EditorUtility.SetDirty(carrier);
            if (carrier.scarecrowAnchors != null)
            {
                foreach (Transform anchor in carrier.scarecrowAnchors)
                {
                    if (anchor != null)
                    {
                        EditorUtility.SetDirty(anchor.gameObject);
                    }
                }
            }
        }

        RoundResultBoardOverlay resultOverlay = Object.FindFirstObjectByType<RoundResultBoardOverlay>(FindObjectsInactive.Include);
        if (resultOverlay != null)
        {
            resultOverlay.gridRenderer = resultOverlay.gridRenderer != null ? resultOverlay.gridRenderer : resultOverlay.GetComponent<PaperBoardGridRenderer>();
            EditorUtility.SetDirty(resultOverlay);
        }

        EditorSceneManager.MarkSceneDirty(EditorSceneManager.GetActiveScene());
        Debug.Log("Prepared the open InGame scene for manual tuning. Adjust HUD layout on PrototypeHud, board grid on PaperBoardTarget, result board on RoundResultBoardOverlay, and scarecrow positions with Scarecrow Anchor objects.");
    }
}
