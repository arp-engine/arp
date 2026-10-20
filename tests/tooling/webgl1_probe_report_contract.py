"""Shared cold-probe report validation; no native execution authority.

Keep Python 3.6 compatibility for the bundled RenderDoc interpreter.
"""


class WebGlProbeReportContract:
    VERSION = 6
    RETIREMENT_FIELDS = (
        "vertexSourceCopied", "fragmentSourceCopied", "shaderAttachmentsMatched",
        "shadersDetached", "shaderNamesRetired", "linkedProgramRetained",
        "framebufferAttachmentsMatched", "framebufferAttachmentsDetached",
        "framebufferBindingsCleared", "framebufferRetiredChildrenLive",
        "renderbufferRetiredTextureLive", "attachmentTextureRetired",
    )
    IDENTITY_FIELDS = ("contextVersion", "shaderVersion", "vendor", "renderer", "browserUserAgent")

    @staticmethod
    def require(condition, diagnostic):
        if not condition:
            raise ValueError("RHI-WEBGL1-PROBE-REPORT-" + diagnostic)

    @staticmethod
    def versioned(value, version):
        return (isinstance(value, dict) and type(value.get("contractVersion")) is int
                and value["contractVersion"] == version)

    @staticmethod
    def version_text(value, prefix):
        return isinstance(value, str) and "\0" not in value and (
            value == prefix or value.startswith(prefix + " "))

    @classmethod
    def validate(cls, result, expected_frames=None):
        cls.require(cls.versioned(result, cls.VERSION)
                    and result.get("type") == "WebGl1RuntimeProbe", "VERSION")
        cls.require(result.get("success") is True, "FAILED")
        cls.require(result.get("gpuCompletionClaimed") is False
                    and result.get("productionBackendValidated") is False, "READINESS")
        cls.require(cls.version_text(result.get("version"), "WebGL 1.0")
                    and cls.version_text(result.get("shadingLanguage"), "WebGL GLSL ES 1.0"), "CONTEXT")
        frames = result.get("issuedFrames")
        cls.require(type(frames) is int and 0 < frames <= 2147483647
                    and (expected_frames is None or frames == expected_frames), "FRAMES")
        for field in ("uploadSourceMutationProbe", "textureSubImageSourceMutationProbe",
                      "bufferSubDataSourceMutationProbe", "issuedObjectDeletionProbe",
                      "precisionFactsValidated"):
            cls.require(result.get(field) is True, field)
        formats = result.get("formatFacts")
        cls.require(cls.versioned(formats, 1) and formats.get("validated") is True, "FORMAT")
        identity = result.get("identityFacts")
        cls.require(cls.versioned(identity, 1) and identity.get("validated") is True
                    and identity.get("sourceAuthenticated") is False, "IDENTITY")
        fields = identity.get("fields")
        cls.require(isinstance(fields, dict), "IDENTITY-FIELDS")
        for field in cls.IDENTITY_FIELDS:
            entry = fields.get(field)
            cls.require(isinstance(entry, dict) and entry.get("queried") is True
                        and isinstance(entry.get("value"), str) and "\0" not in entry["value"],
                        "IDENTITY-" + field)
        cls.require(fields["contextVersion"]["value"] == result["version"]
                    and fields["shaderVersion"]["value"] == result["shadingLanguage"], "IDENTITY-MISMATCH")
        retirement = result.get("retirementFacts")
        cls.require(cls.versioned(retirement, 1), "RETIREMENT-VERSION")
        for field in cls.RETIREMENT_FIELDS:
            cls.require(retirement.get(field) is True, "RETIREMENT-" + field)
